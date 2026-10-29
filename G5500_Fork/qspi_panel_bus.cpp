// G5500 Fork; GPL-3.0.
#include "build_config.hpp"
#if GRAPHICAL_DISPLAY && DISPLAY_KIND == DISPLAY_ST77916
#include "qspi_panel_bus.hpp"
namespace forkrot {
bool QspiPanelBus::begin(const PinMap &p,size_t maxBytes) {
  spi_bus_config_t bus{};
  bus.mosi_io_num=p.tftMosi;bus.miso_io_num=p.tftMiso;bus.sclk_io_num=p.tftSck;
  bus.quadwp_io_num=p.tftD2;bus.quadhd_io_num=p.tftD3;
  bus.data4_io_num=bus.data5_io_num=bus.data6_io_num=bus.data7_io_num=-1;
  bus.max_transfer_sz=maxBytes;
  bus.flags=SPICOMMON_BUSFLAG_MASTER|SPICOMMON_BUSFLAG_QUAD;
  error_=spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO);
  if(error_!=ESP_OK)return false;
  owned_=true;cs_=p.tftCs;
  if(speed(3000000))return true;
  end();return false;
}
bool QspiPanelBus::speed(int hz) {
  if(device_) {
    error_=spi_bus_remove_device(device_);
    if(error_!=ESP_OK)return false;
    device_=nullptr;
  }
  spi_device_interface_config_t config{};
  config.command_bits=8;config.address_bits=24;
  config.clock_speed_hz=hz;config.spics_io_num=cs_;config.queue_size=1;
  config.flags=SPI_DEVICE_HALFDUPLEX;
  error_=spi_bus_add_device(SPI2_HOST,&config,&device_);
  return error_==ESP_OK;
}
bool QspiPanelBus::command(uint8_t reg,const uint8_t *data,size_t bytes) {
  if(!device_)return false;
  spi_transaction_t t{};t.cmd=0x02;t.addr=uint32_t(reg)<<8;
  t.length=bytes*8;t.tx_buffer=data;
  error_=spi_device_polling_transmit(device_,&t);
  return error_==ESP_OK;
}
bool QspiPanelBus::readId(uint8_t (&id)[4]) {
  if(!device_)return false;
  spi_transaction_t t{};t.cmd=0x0b;t.addr=0x04<<8;
  t.rxlength=sizeof id*8;t.flags=SPI_TRANS_USE_RXDATA;
  error_=spi_device_polling_transmit(device_,&t);
  if(error_!=ESP_OK)return false;
  for(unsigned i=0;i<sizeof id;++i)id[i]=t.rx_data[i];
  return true;
}
bool QspiPanelBus::pixels(const uint8_t *data,size_t bytes) {
  if(!device_)return false;
  spi_transaction_t t{};t.cmd=0x32;t.addr=0x2c<<8;
  t.flags=SPI_TRANS_MODE_QIO;t.length=bytes*8;t.tx_buffer=data;
  error_=spi_device_polling_transmit(device_,&t);
  return error_==ESP_OK;
}
void QspiPanelBus::end() {
  if(device_){spi_bus_remove_device(device_);device_=nullptr;}
  if(owned_){spi_bus_free(SPI2_HOST);owned_=false;}
}
}
#endif
