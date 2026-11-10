"""Extend the actual coordinator fixture with outage and fair-delivery cases."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
original=root/'../testing/main/test_automatic_alerts.py'
source=original.read_text(encoding='utf8')
extra=r'''
 {
  blob.clear();clockMs=1000;StateStore s;NEO6MService g;EmergencyCoordinator x(s,g);x.begin();
  CrashProtocol::Frame f;f.type=CrashProtocol::Type::AutomaticCrash;f.boot=55;f.sequence=1;
  assert(x.accept(f,true,0));f.sequence=2;assert(x.accept(f,true,0));
  JsonDocument first;x.cameraCommand(0,first.to<JsonObject>());assert(first["command"]["sequence"]==1);
  assert(x.cameraAck(0,55,1,"received"));
  JsonDocument next;x.cameraCommand(0,next.to<JsonObject>());assert(next["command"]["sequence"]==2);
  assert(x.cameraAck(0,55,2,"received"));
  JsonDocument round;x.cameraCommand(0,round.to<JsonObject>());assert(round["command"]["sequence"]==1);
  x.cameraCommand(0,round.to<JsonObject>());assert(round["command"]["sequence"]==2);
  clockMs+=Config::CAMERA_COMMAND_DEADLINE_MS;x.tick();
  auto state=status(x);assert(state["incidents"][0]["front"]=="received");
  assert(state["incidents"][0]["cameraOverdue"]["front"]==true);
  assert(state["incidents"][0]["cameraOverdue"]["rear"]==true);
  JsonDocument overdue;x.cameraCommand(1,overdue.to<JsonObject>());assert(!overdue["command"].isNull());
  assert(x.cameraAck(0,55,1,"protected"));assert(x.cameraAck(0,55,2,"failed"));
  assert(x.cameraAck(0,55,2,"protected"));assert(!x.cameraAck(0,55,2,"failed"));
  assert(!x.cameraAck(0,55,2,"received"));assert(x.cameraAck(0,55,2,"protected"));
  assert(!x.cameraAck(0,999,2,"protected"));
  state=status(x);assert(state["incidents"][0]["cameraOverdue"]["front"]==false);
  EmergencyCoordinator reboot(s,g);reboot.begin();reboot.cameraCommand(1,overdue.to<JsonObject>());
  assert(!overdue["command"].isNull()&&overdue["command"]["triggerTimeKnown"]==false);
  assert(overdue["command"]["elapsedSinceDetectionMs"].isNull());
  std::cout<<"PASS actual coordinator: pending-first fair scheduling, received round robin, overdue delivery, late protection, failed-to-protected reconciliation, no downgrade, unknown identity and reboot timing\n";
 }
 blob.clear();clockMs=1000;
'''
assert source.count('int main(){')==1
exec(compile(source.replace('int main(){','int main(){\n'+extra,1),str(original),'exec'),{'__file__':str(original),'__name__':'__main__'})
