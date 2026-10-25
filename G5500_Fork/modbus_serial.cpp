#include "modbus_serial.hpp"
#ifdef ARDUINO
#include <Arduino.h>
#include <driver/uart.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <new>
namespace forkrot::mb {
namespace {
struct State {
  uart_port_t port=UART_NUM_0;uint32_t baud=0;unsigned bits=11;
  QueueHandle_t events=nullptr,frames=nullptr;TaskHandle_t task=nullptr;
  Framer framer;
  uint32_t last=0,rejected=0,tx=0;
};
uint32_t now(){return uint32_t(esp_timer_get_time());}
void worker(void *arg) {
  auto &s=*static_cast<State *>(arg);uart_event_t e;
  auto deliver=[&](const Frame &f){if(xQueueSend(s.frames,&f,0)!=pdTRUE)__atomic_add_fetch(&s.rejected,1u,__ATOMIC_RELAXED);};
  for(;;) {
    const bool event=xQueueReceive(s.events,&e,1)==pdTRUE;
    const uint32_t t=now();
    if(__atomic_load_n(&s.tx,__ATOMIC_ACQUIRE)) {
      if(uart_wait_tx_done(s.port,0)==ESP_OK) {
        __atomic_store_n(&s.last,t,__ATOMIC_RELEASE);
        __atomic_store_n(&s.tx,0u,__ATOMIC_RELEASE);s.framer.idle();
      }
    }
    if(event && e.type==UART_DATA && e.size) {
      // A one-byte FIFO threshold keeps timestamping in this high-priority
      // task. If scheduling falls behind, discard rather than invent timing.
      const bool late=e.size!=1 || uxQueueMessagesWaiting(s.events)>2;
      uint8_t byte;
      for(size_t i=0;i<e.size;++i)if(uart_read_bytes(s.port,&byte,1,0)==1) {
        if(!late && !__atomic_load_n(&s.tx,__ATOMIC_ACQUIRE))s.framer.feed(byte,t,s.baud,deliver,s.bits);
      }
      if(late){s.framer.reset(t);__atomic_add_fetch(&s.rejected,1u,__ATOMIC_RELAXED);}
      __atomic_store_n(&s.last,t,__ATOMIC_RELEASE);
    } else if(event && e.type!=UART_DATA) {
      uart_flush_input(s.port);xQueueReset(s.events);s.framer.reset(t);
      __atomic_store_n(&s.last,t,__ATOMIC_RELEASE);__atomic_add_fetch(&s.rejected,1u,__ATOMIC_RELAXED);
    }
    s.framer.poll(t,s.baud,deliver,s.bits);
    if(s.framer.rejected){__atomic_add_fetch(&s.rejected,s.framer.rejected,__ATOMIC_RELAXED);s.framer.rejected=0;}
  }
}
}
SerialPort::~SerialPort(){if(!state_)return;auto *s=static_cast<State *>(state_);if(s->task)vTaskDelete(s->task);uart_driver_delete(s->port);if(s->frames)vQueueDelete(s->frames);delete s;}
bool SerialPort::begin(unsigned port,int rx,int tx,int de,const UartConfig &line) {
  if(!line.valid() || state_ || port>=UART_NUM_MAX || rx<0 || tx<0 || de<0)return false;
  auto *s=new(std::nothrow) State;if(!s)return false;s->port=uart_port_t(port);s->baud=line.baud;s->bits=line.frameBits();s->last=now();s->framer.reset(s->last);
  pinMode(de,OUTPUT);digitalWrite(de,LOW);
  uart_config_t config{};config.baud_rate=int(line.baud);config.data_bits=UART_DATA_8_BITS;
  config.parity=line.parity==1?UART_PARITY_EVEN:line.parity==2?UART_PARITY_ODD:UART_PARITY_DISABLE;
  config.stop_bits=line.stopBits==2?UART_STOP_BITS_2:UART_STOP_BITS_1;config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;config.source_clk=UART_SCLK_DEFAULT;
  bool installed=false;
  if(uart_param_config(s->port,&config)==ESP_OK && uart_set_pin(s->port,tx,rx,de,UART_PIN_NO_CHANGE)==ESP_OK &&
      uart_driver_install(s->port,1024,512,128,&s->events,0)==ESP_OK) {
    installed=true;s->frames=xQueueCreate(4,sizeof(Frame));
    if(s->frames && uart_set_mode(s->port,UART_MODE_RS485_HALF_DUPLEX)==ESP_OK &&
        uart_set_rx_full_threshold(s->port,1)==ESP_OK && uart_set_rx_timeout(s->port,2)==ESP_OK &&
        xTaskCreate(worker,"modbus-rx",4096,s,configMAX_PRIORITIES-3,&s->task)==pdPASS) {state_=s;return true;}
  }
  if(installed)uart_driver_delete(s->port);
  if(s->frames)vQueueDelete(s->frames);
  delete s;return false;
}
bool SerialPort::read(Frame &f){return state_ && xQueueReceive(static_cast<State *>(state_)->frames,&f,0)==pdTRUE;}
bool SerialPort::idle() const {
  if(!state_)return false;auto &s=*static_cast<State *>(state_);
  return !__atomic_load_n(&s.tx,__ATOMIC_ACQUIRE) && uint32_t(now()-__atomic_load_n(&s.last,__ATOMIC_ACQUIRE))>=t35(s.baud,s.bits);
}
bool SerialPort::send(const Frame &f) {
  if(!f.valid()||!idle())return false;auto &s=*static_cast<State *>(state_);
  __atomic_store_n(&s.tx,1u,__ATOMIC_RELEASE);
  const int n=uart_write_bytes(s.port,f.data,f.size);
  if(n!=f.size){__atomic_add_fetch(&s.rejected,1u,__ATOMIC_RELAXED);return false;}
  return true;
}
uint32_t SerialPort::activity()const{return state_?__atomic_load_n(&static_cast<State *>(state_)->last,__ATOMIC_ACQUIRE):0;}
uint32_t SerialPort::rejected()const{return state_?__atomic_load_n(&static_cast<State *>(state_)->rejected,__ATOMIC_RELAXED):0;}
}
#endif
