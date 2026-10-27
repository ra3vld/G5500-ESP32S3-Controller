#include "mqtt_fixture.hpp"
int main(){
 static_assert(MQTT_SUPPORT && !HOME_ASSISTANT_DISCOVERY_SUPPORT);
 Harness h;h.run();
 assert(!h.messages.empty());
 bool online=false,state=false;
 for(const auto &m:h.messages){assert(m.topic.find("homeassistant/")!=0);online|=m.topic.find("/availability")!=std::string::npos&&m.payload=="online";state|=m.topic.find("/state")!=std::string::npos;}
 assert(online&&state&&h.registry.empty());
 assert(h.command("target_az","60"));h.bridge.service(4101);assert(h.runtime.controller.axes[0].target==60);
 assert(h.command("target_az","70",4110));assert(h.command("target_el","80",4110));assert(h.command("stop_az","PRESS",4110));h.bridge.service(4111);
 assert(h.runtime.controller.axes[0].target!=70&&h.runtime.controller.axes[1].target==80);
 std::cout<<"PASS: MQTT without discovery: telemetry, availability, commands and axis-local STOP barriers\n";
}
