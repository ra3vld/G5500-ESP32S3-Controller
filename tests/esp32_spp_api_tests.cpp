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
int main() {
  static_assert(ROTCTLD_SUPPORT && N1MM_SUPPORT && PSTROTATOR_SUPPORT);
  essentialProtocolSettingsTests();
  std::cout << "PASS compact protocol settings: HTTP/wire projection, independent policies, NVS roundtrip and port conflicts\n";
}
