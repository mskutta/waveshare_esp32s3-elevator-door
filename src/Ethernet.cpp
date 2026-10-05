// W5500/ESP-IDF integration adapted from the supplied SpookMMWave project.
#include "Ethernet.h"
#include <atomic>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_eth.h>
#include <esp_eth_netif_glue.h>
#include <esp_event.h>
#include <esp_interface.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_netif_defaults.h>
extern void add_esp_interface_netif(esp_interface_t, esp_netif_t *);
namespace {
std::atomic<bool> linked{false}, gotIP{false};
std::atomic<uint32_t> address{0};
esp_netif_t *ethNetif=nullptr;
esp_eth_handle_t driver=nullptr;
spi_device_handle_t spi=nullptr;
const char *failure="not initialized";
void event(void *, esp_event_base_t base, int32_t id, void *data) {
  if(base==ETH_EVENT) {
    if(id==ETHERNET_EVENT_CONNECTED) linked=true;
    if(id==ETHERNET_EVENT_DISCONNECTED || id==ETHERNET_EVENT_STOP) { linked=false; gotIP=false; address=0; }
  } else if(base==IP_EVENT && id==IP_EVENT_ETH_GOT_IP && data) {
    auto *e=static_cast<ip_event_got_ip_t *>(data);
    address=e->ip_info.ip.addr; gotIP=true;
  }
}
bool check(esp_err_t e,const char *why) { if(e!=ESP_OK) {failure=why;return false;} return true; }
}
bool ethernetRenewDhcp() {
  if(!ethNetif) return false;
  gotIP=false; address=0;
  esp_err_t e=esp_netif_dhcpc_stop(ethNetif);
  if(e!=ESP_OK && e!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {failure="DHCP stop";return false;}
  esp_netif_ip_info_t empty{};
  if(!check(esp_netif_set_ip_info(ethNetif,&empty),"Clear IP")) return false;
  if(!check(esp_netif_dhcpc_start(ethNetif),"DHCP start")) return false;
  failure=""; return true;
}
bool ethernetBegin(const char *hostname) {
  esp_err_t e=esp_netif_init();
  if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) {failure="ethNetif init";return false;}
  e=esp_event_loop_create_default();
  if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) {failure="event loop";return false;}
  spi_bus_config_t bus{};
  bus.miso_io_num=12;bus.mosi_io_num=11;bus.sclk_io_num=13;bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=4096;
  if(!check(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO),"SPI init")) return false;
  spi_device_interface_config_t dev{};
  dev.command_bits=16;dev.address_bits=8;dev.clock_speed_hz=12000000;dev.spics_io_num=14;dev.queue_size=20;
  if(!check(spi_bus_add_device(SPI2_HOST,&dev,&spi),"SPI device")) return false;
  e=gpio_install_isr_service(0);
  if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) {failure="GPIO ISR service";return false;}
  eth_mac_config_t mc=ETH_MAC_DEFAULT_CONFIG();
  eth_w5500_config_t wc=ETH_W5500_DEFAULT_CONFIG(spi);wc.int_gpio_num=10;
  esp_eth_mac_t *mac=esp_eth_mac_new_w5500(&wc,&mc);
  eth_phy_config_t pc=ETH_PHY_DEFAULT_CONFIG();pc.phy_addr=1;pc.reset_gpio_num=9;
  esp_eth_phy_t *phy=esp_eth_phy_new_w5500(&pc);
  if(!mac || !phy) {failure="W5500 MAC/PHY";return false;}
  esp_eth_config_t eth=ETH_DEFAULT_CONFIG(mac,phy);
  if(!check(esp_eth_driver_install(&eth,&driver),"Ethernet driver")) return false;
  uint8_t hw[6];
  if(!check(esp_read_mac(hw,ESP_MAC_ETH),"Ethernet MAC") || !check(esp_eth_ioctl(driver,ETH_CMD_S_MAC_ADDR,hw),"Set MAC")) return false;
  esp_netif_config_t nc=ESP_NETIF_DEFAULT_ETH();ethNetif=esp_netif_new(&nc);
  if(!ethNetif) {failure="Ethernet ethNetif";return false;}
  if(!check(esp_event_handler_register(ETH_EVENT,ESP_EVENT_ANY_ID,event,nullptr),"Ethernet events") ||
     !check(esp_event_handler_register(IP_EVENT,IP_EVENT_ETH_GOT_IP,event,nullptr),"IP events")) return false;
  auto glue=esp_eth_new_netif_glue(driver);
  if(!glue || !check(esp_netif_attach(ethNetif,glue),"Attach ethNetif")) return false;
  add_esp_interface_netif(ESP_IF_ETH,ethNetif);
  if(!check(esp_netif_set_hostname(ethNetif,hostname),"Hostname")) return false;
  if(!ethernetRenewDhcp()) return false;
  return check(esp_eth_start(driver),"Ethernet start");
}
bool ethernetReady() {return linked.load() && gotIP.load();}
IPAddress ethernetIP() {return IPAddress(address.load());}
const char *ethernetError() {return failure;}
