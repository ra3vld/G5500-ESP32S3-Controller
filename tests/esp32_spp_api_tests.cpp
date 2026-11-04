#include "device_service_fixture.hpp"
static void essentialProtocolSettingsTests() {
  for(bool wire:{false,true}) {
    Fixture f;auto &r=f.runtime;r.hud.config.role=DeviceRole::Controller;r.controller.remote=nullptr;
    r.controller.driveFault=false;r.controller.fault.clear();r.controller.feedback(90,45,true,true);
    assert(f.call("POST","/api/settings","n1mmPort=12041&pstPort=1236&reportPort=13011&reportHost=192.0.2.15&n1mmManualFollow=1&pstManualFollow=0&rotctldManualFollow=1&gs232ManualFollow=0",wire).status==200);
    const auto response=f.call("GET","/api/config","view=essential",wire);
    assert(response.status==200 && response.body.size()<=COMMAND_PAGE_MAX);
    for(const char *field:{"\"n1mmPort\":12041","\"pstPort\":1236","\"reportPort\":13011","\"reportHost\":\"192.0.2.15\"","\"n1mmManualFollow\":true","\"pstManualFollow\":false","\"rotctldManualFollow\":true","\"gs232ManualFollow\":false"})
      assert(response.body.find(field)!=response.body.npos);
    Runtime loaded;f.storage.loadForkSettings(loaded);
    assert(loaded.loggerSettings.n1mmPort==12041 && loaded.loggerSettings.pstPort==1236);
    assert(loaded.loggerSettings.reportPort==13011 && loaded.loggerSettings.reportHost=="192.0.2.15");
    assert(loaded.protocol.manualFollow.n1mm && loaded.protocol.manualFollow.rotctld);
    assert(!loaded.protocol.manualFollow.pst && !loaded.protocol.manualFollow.gs232);
    assert(r.protocol.datagram("<PST><AZIMUTH>100</AZIMUTH></PST>",fakeHardware.now).result==Result::Pending);
    const auto epoch=r.controller.stopEpoch;
    assert(f.call("POST","/api/settings","reportHost=",wire).status==200);
    assert(r.controller.pendingValid && r.controller.pending.az==100 && r.controller.stopEpoch==epoch);
    assert(!r.controller.moving()); // Saving protocol settings never executes a pending target.
    const auto before=Preferences::database;
    assert(f.call("POST","/api/settings","n1mmPort=4534",wire).status==400);
    assert(Preferences::database==before);
  }
}

static void essentialStatusTests() {
  for(bool wire:{false,true}) {
    Fixture f;auto &c=f.runtime.controller;
    for(auto model:{Model::G5500,Model::G800DXA,Model::G800SA}) {
      c.setModel(model);c.feedback(90,45,true,model==Model::G5500);
      const auto response=f.call("GET","/api/status","view=minimal",wire);
      assert(response.status==200 && response.body.size()<=COMMAND_PAGE_MAX);
      assert(response.body.find(model==Model::G5500?"\"hasEl\":true":"\"hasEl\":false")!=std::string::npos);
      assert(response.body.find("\"version\":")!=std::string::npos && response.body.find("\"commit\":")!=std::string::npos);
      assert(f.call("POST","/api/mode","mode=auto",wire).status==200);
      assert(f.call("POST","/api/target","az=100&rangeRevision="+std::to_string(c.rangeRevision),wire).status==200);
      assert(c.axes[0].tracking && c.axes[0].target==100);
      assert(f.call("POST","/api/move","d=S",wire).status==200 && !c.moving());
    }
  }
}
static void essentialNetworkTests() {
  for(bool wire:{false,true}) {
    {
      Fixture f;auto &r=f.runtime;
      assert(f.call("POST","/api/move","d=R",wire).status==200);const auto epoch=r.controller.stopEpoch;const auto db=Preferences::database;
      assert(f.call("POST","/api/network","choice=0&wifiMode=static&wifiAddress=10.2.3.4&wifiMask=255.0.255.0",wire).status==400);
      assert(r.controller.stopEpoch==epoch && Preferences::database==db);
      assert(f.call("POST","/api/network","choice=0&wifiMode=static&wifiAddress=10.2.3.4&wifiMask=255.255.255.0&wifiGateway=10.2.3.1&wifiDns1=9.9.9.9&wifiDns2=",wire).status==200);
      assert(!r.controller.moving() && r.networkTrial && !r.networkTrialBooted && f.restart.pending());
      NetworkConfig saved;assert(f.storage.loadNetwork(saved));assert(!saved.wifi.automatic);
      uint32_t expected;assert(parseIpv4("10.2.3.4",expected)&&saved.wifi.address==expected);
      assert(parseIpv4("9.9.9.9",expected)&&saved.wifi.dns1==expected&&!saved.wifi.dns2);
      auto response=f.call("GET","/api/network","",wire);
      assert(response.body.find("\"trial\":true")!=std::string::npos && response.body.find("\"token\":11")!=std::string::npos);
      assert(f.call("POST","/api/network/confirm","token=11&address=192.168.4.1",wire).status==409);
      assert(f.storage.loadNetworkTrial(r));f.restart=RestartDelay{};WiFi.connectionStatus=WL_CONNECTED;
      assert(f.call("POST","/api/network/confirm","token=11&address=10.2.3.4",wire).status==409);
      assert(f.call("POST","/api/network/rollback","token=10",wire).status==409);
      assert(f.call("POST","/api/network/rollback","token=11",wire).status==200);
      assert(f.storage.loadNetwork(saved)&&saved.wifi.automatic);
    }
    {
      Fixture f;auto &r=f.runtime;
      assert(f.call("POST","/api/network","choice=0&wifiMode=dhcp",wire).status==200);
      assert(f.storage.loadNetworkTrial(r));f.restart=RestartDelay{};WiFi.connectionStatus=WL_CONNECTED;f.restart=RestartDelay{};
      const auto fields="token=11&address="+std::string(WiFi.localIP().toString().c_str());
      Preferences::failedRemoveKey="trial1";assert(f.call("POST","/api/network/confirm",fields,wire).status==503&&r.networkTrial);
      Preferences::failedRemoveKey.clear();assert(f.call("POST","/api/network/confirm",fields,wire).status==200&&!r.networkTrial);
      fakeHardware.now+=Runtime::NetworkTrialMs;f.api.serviceNetworkTrial(fakeHardware.now);assert(!f.restart.pending());
    }
    {
      Fixture f;const auto network=f.network.credentials().network;
      assert(f.call("POST","/api/wifi","ssid=New+Home&pass=new-password",wire).status==200);
      assert(f.network.credentials().wifiSsid=="New Home");
      assert(encodeNetwork(f.network.credentials().network)==encodeNetwork(network));
      assert(f.call("POST","/api/ap","pass=short",wire).status==400);
      assert(f.call("POST","/api/ap","pass=ap-password",wire).status==200);
      assert(std::get<String>(Preferences::database["g5500"]["appass"])=="ap-password");
      assert(f.call("POST","/api/ap","pass=",wire).status==200);
      assert(std::get<String>(Preferences::database["g5500"]["appass"]).length()==0);
    }
  }
}
int main() {
  static_assert(ROTCTLD_SUPPORT && N1MM_SUPPORT && PSTROTATOR_SUPPORT);
  essentialProtocolSettingsTests();
  essentialStatusTests();
  essentialNetworkTests();
  std::cout << "PASS compact protocol settings: HTTP/wire projection, independent policies, NVS roundtrip and port conflicts\n";
}
