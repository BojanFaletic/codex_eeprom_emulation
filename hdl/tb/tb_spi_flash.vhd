library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb_spi_flash is
end entity;

architecture tb of tb_spi_flash is
  constant CLK_PERIOD : time := 10 ns;

  signal clk    : std_logic := '0';
  signal reset_n: std_logic := '0';
  signal sclk   : std_logic := '0';
  signal cs_n   : std_logic := '1';
  signal mosi   : std_logic := '0';
  signal miso   : std_logic;

  -- DUT
  component spi_flash
    generic (
      MEM_BYTES          : integer := 65536;
      PAGE_SIZE          : integer := 256;
      SECTOR_SIZE        : integer := 4096;
      PROG_BUSY_CYCLES   : integer := 50;
      ERASE_BUSY_CYCLES  : integer := 200
    );
    port (
      clk    : in  std_logic;
      reset_n: in  std_logic;
      sclk   : in  std_logic;
      cs_n   : in  std_logic;
      mosi   : in  std_logic;
      miso   : out std_logic
    );
  end component;

  -- helpers
  procedure spi_send_byte(signal sclk: out std_logic; signal mosi: out std_logic; constant b: std_logic_vector(7 downto 0)) is
  begin
    for i in 7 downto 0 loop
      -- setup data before rising edge
      mosi <= b(i);
      wait for CLK_PERIOD/4;
      sclk <= '1';
      wait for CLK_PERIOD/2;
      sclk <= '0';
      wait for CLK_PERIOD/4;
    end loop;
  end procedure;

  procedure spi_recv_byte(signal sclk: out std_logic; signal mosi: out std_logic; signal miso: in std_logic; b: out std_logic_vector(7 downto 0)) is
  begin
    for i in 7 downto 0 loop
      -- present dummy 0 on MOSI
      mosi <= '0';
      wait for CLK_PERIOD/4;
      sclk <= '1';
      -- sample mid-high (mode 0) to avoid delta-cycle races
      wait for (3*CLK_PERIOD/8);
      b(i) := miso;
      wait for (CLK_PERIOD/2 - 3*CLK_PERIOD/8);
      sclk <= '0';
      wait for CLK_PERIOD/4;
    end loop;
  end procedure;

  -- generate one extra SCLK cycle to align first MISO bit for READ/RDSR streams
  procedure spi_prime(signal sclk: out std_logic; signal mosi: out std_logic) is
  begin
    mosi <= '0';
    wait for CLK_PERIOD/4; sclk <= '1';
    wait for CLK_PERIOD/2; sclk <= '0';
    wait for CLK_PERIOD/4;
  end procedure;

  -- commands
  constant CMD_WREN : std_logic_vector(7 downto 0) := x"06";
  constant CMD_RDSR : std_logic_vector(7 downto 0) := x"05";
  constant CMD_READ : std_logic_vector(7 downto 0) := x"03";
  constant CMD_PP   : std_logic_vector(7 downto 0) := x"02";
  constant CMD_SE   : std_logic_vector(7 downto 0) := x"20";

  -- convenience: issue READ command for a single byte at 24-bit address
  procedure spi_read1(
    signal sclk: out std_logic;
    signal cs_n: out std_logic;
    signal mosi: out std_logic;
    signal miso: in  std_logic;
    constant a23_16: std_logic_vector(7 downto 0);
    constant a15_8 : std_logic_vector(7 downto 0);
    constant a7_0  : std_logic_vector(7 downto 0);
    b: out std_logic_vector(7 downto 0)
  ) is
  begin
    cs_n <= '0';
    report "TB spi_read1: send READ" severity note;
    spi_send_byte(sclk, mosi, CMD_READ);
    spi_send_byte(sclk, mosi, a23_16);
    spi_send_byte(sclk, mosi, a15_8);
    spi_send_byte(sclk, mosi, a7_0);
    spi_prime(sclk, mosi);
    spi_recv_byte(sclk, mosi, miso, b);
    cs_n <= '1';
    wait for CLK_PERIOD;
  end procedure;

  -- commands moved above for visibility in procedures

begin
  -- clk
  clk <= not clk after CLK_PERIOD/2;

  -- DUT inst
  dut: spi_flash
    generic map (
      MEM_BYTES => 65536,
      PAGE_SIZE => 256,
      SECTOR_SIZE => 4096,
      PROG_BUSY_CYCLES => 50,
      ERASE_BUSY_CYCLES => 200
    )
    port map (
      clk => clk,
      reset_n => reset_n,
      sclk => sclk,
      cs_n => cs_n,
      mosi => mosi,
      miso => miso
    );

  stim: process
    variable status : std_logic_vector(7 downto 0);
    variable byte   : std_logic_vector(7 downto 0);
  begin
    -- reset
    reset_n <= '0';
    wait for 10*CLK_PERIOD;
    reset_n <= '1';
    wait for 5*CLK_PERIOD;

    -- Read status: expect WIP=0, WEL=0
    cs_n <= '0';
    spi_send_byte(sclk, mosi, CMD_RDSR);
    spi_prime(sclk, mosi);
    spi_recv_byte(sclk, mosi, miso, status);
    cs_n <= '1';
    wait for CLK_PERIOD;
    assert status(0) = '0' and status(1) = '0' report "RDSR after reset incorrect" severity failure;

    -- WREN
    cs_n <= '0';
    spi_send_byte(sclk, mosi, CMD_WREN);
    cs_n <= '1';
    wait for 2*CLK_PERIOD;

    -- Check WEL=1
    cs_n <= '0';
    spi_send_byte(sclk, mosi, CMD_RDSR);
    spi_recv_byte(sclk, mosi, miso, status);
    cs_n <= '1';
    wait for CLK_PERIOD;
    report "RDSR after WREN=" & to_hstring(status) severity note;
    assert status(1) = '1' report "WEL not set after WREN" severity failure;

    -- Page program 4 bytes at addr 0x000010
    cs_n <= '0';
    spi_send_byte(sclk, mosi, CMD_PP);
    spi_send_byte(sclk, mosi, x"00");
    spi_send_byte(sclk, mosi, x"00");
    spi_send_byte(sclk, mosi, x"10");
    spi_send_byte(sclk, mosi, x"AA");
    spi_send_byte(sclk, mosi, x"55");
    spi_send_byte(sclk, mosi, x"00");
    spi_send_byte(sclk, mosi, x"FF");
    cs_n <= '1';

    -- Expect busy for PROG_BUSY_CYCLES: poll until WIP=1
    wait for 2*CLK_PERIOD;
    for k in 0 to 10 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      exit when status(0) = '1';
      wait for CLK_PERIOD;
    end loop;
    assert status(0) = '1' report "WIP not set during program" severity failure;
    -- wait until WIP clears
    for k in 0 to 50 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      exit when status(0) = '0';
      wait for CLK_PERIOD;
    end loop;
    assert status(0) = '0' report "WIP not cleared after program" severity failure;

    -- Ensure a short CS-high time between transactions
    wait for CLK_PERIOD;

    -- Read back bytes individually
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"10", byte); report "READ byte0=" & to_hstring(byte) severity note; assert byte = x"AA" report "READ mismatch byte0" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"11", byte); assert byte = x"55" report "READ mismatch byte1" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"12", byte); assert byte = x"00" report "READ mismatch byte2" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"13", byte); assert byte = x"FF" report "READ mismatch byte3" severity failure;

    -- Try PP without WREN at 0x000020 (should not change and not set WIP)
    cs_n <= '0';
    spi_send_byte(sclk, mosi, CMD_PP);
    spi_send_byte(sclk, mosi, x"00");
    spi_send_byte(sclk, mosi, x"00");
    spi_send_byte(sclk, mosi, x"20");
    spi_send_byte(sclk, mosi, x"12");
    spi_send_byte(sclk, mosi, x"34");
    cs_n <= '1';
    wait for 2*CLK_PERIOD;
    for k in 0 to 5 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      assert status(0) = '0' and status(1) = '0' report "Unexpected WIP/WEL after PP without WREN" severity failure;
      wait for CLK_PERIOD;
    end loop;
    -- Read back should be FF
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"20", byte); assert byte = x"FF" report "PP without WREN changed memory (byte0)" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"21", byte); assert byte = x"FF" report "PP without WREN changed memory (byte1)" severity failure;
    cs_n <= '1';

    -- Program across page boundary: start at 0x0000FE write 4 bytes -> only first 2 should program
    -- WREN
    cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_WREN); cs_n <= '1'; wait for 2*CLK_PERIOD;
    -- PP 0xDE,0xAD,0xBE,0xEF at 0x0000FE
    cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_PP);
    spi_send_byte(sclk, mosi, x"00"); spi_send_byte(sclk, mosi, x"00"); spi_send_byte(sclk, mosi, x"FE");
    spi_send_byte(sclk, mosi, x"DE"); spi_send_byte(sclk, mosi, x"AD"); spi_send_byte(sclk, mosi, x"BE"); spi_send_byte(sclk, mosi, x"EF");
    cs_n <= '1';
    -- wait busy cycle
    wait for 2*CLK_PERIOD;
    for k in 0 to 10 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      exit when status(0) = '0';
      wait for CLK_PERIOD;
    end loop;
    -- Read back 4 bytes from 0x0000FE: expect DE, AD, FF, FF
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"FE", byte); assert byte = x"DE" report "Page-boundary PP mismatch (byte0)" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"FF", byte); assert byte = x"AD" report "Page-boundary PP mismatch (byte1)" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"01", x"00", byte); assert byte = x"FF" report "Page-boundary PP mismatch (byte2)" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"01", x"01", byte); assert byte = x"FF" report "Page-boundary PP mismatch (byte3)" severity failure;

    -- Second program AND behavior at 0x000010: program F0,0F,FF,00 and expect AND with existing
    cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_WREN); cs_n <= '1'; wait for 2*CLK_PERIOD;
    cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_PP);
    spi_send_byte(sclk, mosi, x"00"); spi_send_byte(sclk, mosi, x"00"); spi_send_byte(sclk, mosi, x"10");
    spi_send_byte(sclk, mosi, x"F0"); spi_send_byte(sclk, mosi, x"0F"); spi_send_byte(sclk, mosi, x"FF"); spi_send_byte(sclk, mosi, x"00");
    cs_n <= '1';
    -- wait for complete
    wait for 2*CLK_PERIOD;
    for k in 0 to 10 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      exit when status(0) = '0';
      wait for CLK_PERIOD;
    end loop;
    -- Read back expect A0,05,00,00
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"10", byte); assert byte = x"A0" report "AND behavior mismatch (byte0)" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"11", byte); assert byte = x"05" report "AND behavior mismatch (byte1)" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"12", byte); assert byte = x"00" report "AND behavior mismatch (byte2)" severity failure;
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"13", byte); assert byte = x"00" report "AND behavior mismatch (byte3)" severity failure;

    -- Sector erase at 0x000000
    -- WREN first
    cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_WREN); cs_n <= '1'; wait for 2*CLK_PERIOD;
    cs_n <= '0';
    spi_send_byte(sclk, mosi, CMD_SE);
    spi_send_byte(sclk, mosi, x"00");
    spi_send_byte(sclk, mosi, x"00");
    spi_send_byte(sclk, mosi, x"00");
    cs_n <= '1';

    wait for 3*CLK_PERIOD;
    -- poll until WIP=1 then until WIP=0
    for k in 0 to 10 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      exit when status(0) = '1';
      wait for CLK_PERIOD;
    end loop;
    assert status(0) = '1' report "WIP not set during erase" severity failure;

    for k in 0 to 200 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      exit when status(0) = '0';
      wait for CLK_PERIOD;
    end loop;
    assert status(0) = '0' report "WIP not cleared after erase" severity failure;

    -- Read back erased location 0x000010 should be FF (since in sector 0)
    spi_read1(sclk, cs_n, mosi, miso, x"00", x"00", x"10", byte);
    assert byte = x"FF" report "Erase did not set to 0xFF" severity failure;

    -- Attempt SE without WREN at sector 1 base (0x001000) should do nothing
    cs_n <= '0';
    spi_send_byte(sclk, mosi, CMD_SE);
    spi_send_byte(sclk, mosi, x"00"); spi_send_byte(sclk, mosi, x"10"); spi_send_byte(sclk, mosi, x"00");
    cs_n <= '1';
    wait for 3*CLK_PERIOD;
    for k in 0 to 5 loop
      cs_n <= '0'; spi_send_byte(sclk, mosi, CMD_RDSR); spi_recv_byte(sclk, mosi, miso, status); cs_n <= '1';
      assert status(0) = '0' and status(1) = '0' report "Unexpected WIP/WEL for SE without WREN" severity failure;
      wait for CLK_PERIOD;
    end loop;

    -- Done
    report "TB completed" severity note;
    wait;
  end process;

end architecture;
