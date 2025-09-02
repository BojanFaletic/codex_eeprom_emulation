library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Behavioral SPI NOR flash model (mode 0):
-- - Commands: WREN(0x06), RDSR(0x05), READ(0x03), PP(0x02), SE(0x20)
-- - WEL and WIP status bits
-- - Program: only 1->0; stops at page boundary
-- - Erase: sets sector to 0xFF
-- - Busy timing driven by generic cycles and a separate clk

entity spi_flash is
  generic (
    MEM_BYTES          : integer := 65536;
    PAGE_SIZE          : integer := 256;
    SECTOR_SIZE        : integer := 4096;
    PROG_BUSY_CYCLES   : integer := 100;
    ERASE_BUSY_CYCLES  : integer := 2000
  );
  port (
    clk    : in  std_logic;            -- system clock for busy timing
    reset_n: in  std_logic;            -- async reset, active low
    sclk   : in  std_logic;            -- SPI clock (mode 0)
    cs_n   : in  std_logic;            -- chip select, active low
    mosi   : in  std_logic;            -- master out
    miso   : out std_logic             -- master in
  );
end entity;

architecture behav of spi_flash is
  subtype byte_t is std_logic_vector(7 downto 0);
  type mem_t is array (natural range <>) of byte_t;

  -- Memory storage
  signal mem : mem_t(0 to MEM_BYTES-1);

  -- Status bits
  signal wel      : std_logic := '0';
  signal wip      : std_logic := '0';
  signal busy_cnt : integer := 0;
  signal start_prog  : std_logic := '0';
  signal start_erase : std_logic := '0';

  -- SPI state
  type state_t is (IDLE, ADDR, READ_DATA, PP_DATA, RDSR_STREAM, IGNORE);
  signal st : state_t := IDLE;

  signal cmd_reg   : byte_t := (others => '0');
  signal addr_reg  : std_logic_vector(23 downto 0) := (others => '0');
  signal addr_tmp  : unsigned(23 downto 0) := (others => '0');
  signal rx_shift  : byte_t := (others => '0');
  signal tx_shift  : byte_t := (others => '0');
  signal rx_bitcnt : integer range 0 to 8 := 0;
  signal tx_bitidx : integer range 0 to 7 := 7; -- bit index for tx_shift
  signal addr_bytes_needed : integer range 0 to 3 := 0;

  signal page_limit_addr : unsigned(23 downto 0) := (others => '0');
  signal bytes_programmed_in_txn : integer := 0;

  -- Edge detect handled with process-local variables for robust zero-time CS toggles

  -- helpers
  function to_byte(u : unsigned) return byte_t is
  begin
    return std_logic_vector(u(7 downto 0));
  end;

begin

  -- Busy timing on system clock
  busy_proc: process(clk, reset_n)
  begin
    if reset_n = '0' then
      busy_cnt <= 0;
      wip <= '0';
    elsif rising_edge(clk) then
      if start_prog = '1' then
        report "start_prog -> busy" severity note;
        busy_cnt <= PROG_BUSY_CYCLES;
        wip <= '1';
      elsif start_erase = '1' then
        report "start_erase -> busy" severity note;
        busy_cnt <= ERASE_BUSY_CYCLES;
        wip <= '1';
      elsif busy_cnt > 0 then
        busy_cnt <= busy_cnt - 1;
        if busy_cnt = 1 then
          report "busy complete" severity note;
          wip <= '0';
        end if;
      end if;
    end if;
  end process;

  -- Main SPI logic
  spi_proc: process(sclk, cs_n, reset_n, wip)
    variable addr_acc   : std_logic_vector(23 downto 0);
    variable sector_base: integer;
    variable rx_byte    : std_logic_vector(7 downto 0);
    variable addr_int   : integer;
    variable status     : std_logic_vector(7 downto 0);
    variable cur        : byte_t;
    variable addr_bytes_left : integer range 0 to 3 := 0;
    variable sclk_prev_v : std_logic := '0';
    variable cs_prev_v   : std_logic := '1';
  begin
    if reset_n = '0' then
      st <= IDLE;
      cmd_reg <= (others => '0');
      addr_reg <= (others => '0');
      rx_shift <= (others => '0');
      tx_shift <= (others => '0');
      rx_bitcnt <= 0;
      tx_bitidx <= 7;
      addr_bytes_needed <= 0;
      wel <= '0';
      cs_prev_v := '1';
      sclk_prev_v := '0';
      bytes_programmed_in_txn <= 0;
      start_prog <= '0';
      start_erase <= '0';
      for i in 0 to MEM_BYTES-1 loop
        mem(i) <= (others => '1');
      end loop;
      miso <= 'Z';
    else
      -- CS edge handling
      if cs_prev_v = '0' and cs_n = '1' then
        -- rising edge: end of transaction
        report "CS rising, end of txn. cmd_reg=" & integer'image(to_integer(unsigned(cmd_reg))) & " st=" & integer'image(state_t'pos(st)) severity note;
        if cmd_reg = x"02" then -- PP
          if wel = '1' and bytes_programmed_in_txn > 0 then
            -- Enter busy and clear WEL
            wel <= '0';
            start_prog <= '1';
          end if;
        elsif cmd_reg = x"20" then -- SE
          if wel = '1' then
            -- perform erase now and then become busy
            sector_base := (to_integer(unsigned(addr_reg)) / SECTOR_SIZE) * SECTOR_SIZE;
            for i in sector_base to sector_base + SECTOR_SIZE - 1 loop
              if i >= 0 and i < MEM_BYTES then
                mem(i) <= (others => '1');
              end if;
            end loop;
            wel <= '0';
            start_erase <= '1';
          end if;
        end if;
        st <= IDLE;
        rx_bitcnt <= 0;
        addr_bytes_needed <= 0;
        addr_bytes_left := 0;
        bytes_programmed_in_txn <= 0;
        miso <= 'Z';
      elsif cs_prev_v = '1' and cs_n = '0' then
        -- falling edge: start of transaction
        rx_bitcnt <= 0;
        tx_bitidx <= 7;
        addr_bytes_needed <= 0;
        addr_bytes_left := 0;
        bytes_programmed_in_txn <= 0;
      end if;
      cs_prev_v := cs_n;

      -- clear start strobes once busy asserted
      if wip = '1' then
        start_prog <= '0';
        start_erase <= '0';
      end if;

      if cs_n = '0' then
        -- SPI active
        -- Shift out on falling edge of SCLK (mode 0)
        if sclk_prev_v = '1' and sclk = '0' then
          if st = RDSR_STREAM then
            status := (7 downto 2 => '0') & wel & wip;
            if tx_bitidx = 0 then
              report "RDSR status byte about to output LSB=" & integer'image(to_integer(unsigned(status))) severity note;
            end if;
            miso <= status(tx_bitidx);
          elsif st = READ_DATA then
            -- Ensure first READ byte is available at the very first falling edge
            if tx_bitidx = 7 then
              cur := mem(to_integer(addr_tmp));
              report "READ cur addr=" & integer'image(to_integer(addr_tmp)) & " val=" & integer'image(to_integer(unsigned(cur))) severity note;
              tx_shift <= cur; -- schedule for subsequent bits
            else
              cur := tx_shift;
            end if;
            miso <= cur(tx_bitidx);
          else
            miso <= tx_shift(tx_bitidx);
          end if;

          if tx_bitidx = 0 then
            tx_bitidx <= 7;
            -- reload/advance for next byte
            if st = READ_DATA then
              addr_tmp <= addr_tmp + 1;
            else
              tx_shift <= (others => '0');
            end if;
          else
            tx_bitidx <= tx_bitidx - 1;
          end if;
        end if;

        -- Sample MOSI on rising edge of SCLK
        if sclk_prev_v = '0' and sclk = '1' then
          rx_shift <= rx_shift(6 downto 0) & mosi;
          rx_bitcnt <= rx_bitcnt + 1;
          if rx_bitcnt = 7 then
            -- Complete byte received in rx_shift & mosi
            rx_byte := rx_shift(6 downto 0) & mosi;
            case st is
              when IDLE =>
                cmd_reg <= rx_byte;
                report "CMD byte received=" & integer'image(to_integer(unsigned(rx_byte))) severity note;
                -- interpret command
                if rx_byte = x"06" then -- WREN
                  report "CMD WREN" severity note;
                  if wip = '0' then
                    wel <= '1';
                  end if;
                  st <= IGNORE; -- no further bytes needed
                elsif rx_byte = x"05" then -- RDSR
                  report "CMD RDSR" severity note;
                  st <= RDSR_STREAM;
                  tx_bitidx <= 7;
                elsif rx_byte = x"03" then -- READ
                  report "CMD READ" severity note;
                  st <= ADDR;
                  addr_reg <= (others => '0');
                  addr_bytes_needed <= 3;
                  addr_bytes_left := 3;
                elsif rx_byte = x"02" then -- PP
                  report "CMD PP" severity note;
                  st <= ADDR;
                  addr_reg <= (others => '0');
                  addr_bytes_needed <= 3;
                  addr_bytes_left := 3;
                elsif rx_byte = x"20" then -- SE
                  report "CMD SE" severity note;
                  st <= ADDR;
                  addr_reg <= (others => '0');
                  addr_bytes_needed <= 3;
                  addr_bytes_left := 3;
                else
                  st <= IGNORE; -- unsupported command
                end if;
              when ADDR =>
                addr_reg(23 downto 8) <= addr_reg(15 downto 0);
                addr_reg(7 downto 0)  <= rx_byte;
                report "ADDR byte rx=" & integer'image(to_integer(unsigned(rx_byte))) & " left(before dec)=" & integer'image(addr_bytes_left) severity note;
                if addr_bytes_left > 0 then
                  addr_bytes_left := addr_bytes_left - 1;
                end if;
                report "ADDR left(after dec)=" & integer'image(addr_bytes_left) severity note;
                if addr_bytes_left = 0 then
                  -- full address captured
                  addr_int := (to_integer(unsigned(addr_reg(15 downto 0))) * 256) + to_integer(unsigned(rx_byte));
                  addr_tmp <= to_unsigned(addr_int, 24);
                  if cmd_reg = x"03" then
                    st <= READ_DATA;
                    tx_shift <= mem(addr_int);
                    tx_bitidx <= 7;
                  elsif cmd_reg = x"02" then
                    st <= PP_DATA;
                    -- compute address and next page boundary
                    addr_tmp <= to_unsigned(addr_int, 24);
                    page_limit_addr <= to_unsigned(((addr_int / PAGE_SIZE) * PAGE_SIZE) + PAGE_SIZE, 24);
                  elsif cmd_reg = x"20" then
                    st <= IGNORE; -- payload none; actual erase on CS rising
                  end if;
                end if;
              when READ_DATA =>
                -- ignore MOSI during read
                null;
                -- advance address at every completed tx byte (handled in tx reload)
                if tx_bitidx = 0 then
                  addr_tmp <= addr_tmp + 1;
                end if;
              when PP_DATA =>
                -- receive data bytes to program; on every byte, AND into mem until page boundary
                -- guard: only if WEL and not WIP
                if wel = '1' and wip = '0' then
                  -- compute if within page
                  if unsigned(addr_tmp) < page_limit_addr then
                    -- perform programming of this byte
                    -- 1->0 only: mem := mem and data
                    report "PP write addr=" & integer'image(to_integer(addr_tmp)) & " data=" & integer'image(to_integer(unsigned(rx_shift(6 downto 0) & mosi))) severity note;
                    mem(to_integer(addr_tmp)) <= mem(to_integer(addr_tmp)) and (rx_shift(6 downto 0) & mosi);
                    addr_tmp <= addr_tmp + 1;
                    bytes_programmed_in_txn <= bytes_programmed_in_txn + 1;
                  end if;
                end if;
              when RDSR_STREAM =>
                -- ignore MOSI
                null;
              when IGNORE =>
                null;
            end case;
            rx_bitcnt <= 0;
          end if;
        end if; -- rising edge

      else
        miso <= 'Z';
      end if; -- cs low

      sclk_prev_v := sclk;
    end if; -- reset
  end process;

end architecture;
