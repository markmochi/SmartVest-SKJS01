// The sensing heartbeat and the detailed IMU stream are two different signals.
// A fresh ImuReady heartbeat proves the sensing firmware considers the sensor
// healthy; missing live telemetry must not be reported as a chip failure.
export function imuAvailability(imu={},link={},profileCompatible=true){
 if(!imu.online){
  if(!link.online)return {status:'Sensing board offline',reason:'No sensing-board heartbeat. Verify crossed UART wiring, shared ground, 115200 baud and that the main and sensing sketches are both flashed.'};
  if(link.imuReady)return {status:'BMI323 ready · detailed live readings unavailable',reason:'The sensing board reports fresh IMU measurements, but the main board has not received compatible live BMI323 data. Check that both boards run this same project firmware, and inspect UART framing and handshake counters below.'};
  return {status:'Sensing board connected · IMU not ready',reason:'The sensing board is connected but reports no fresh IMU measurements. Open the sensing ESP32 Serial Monitor at 115200 baud and inspect [BMI] and [SPI] lines.'};
 }
 if(!profileCompatible)return {status:'Incompatible IMU profile',reason:'Detailed telemetry is present, but its expected BMI323 data profile does not match.'};
 if(imu.healthy)return {status:'IMU online · BMI323 SPI',reason:'Current BMI323 measurement stream verified.'};
 return {status:'IMU unavailable',reason:'The sensor is not delivering fresh validated samples; inspect sensing acquisition diagnostics.'};
}
export function profileFor(m){
 const p=m?.profile;
 if(m?.schemaVersion!==2||p?.sensorModel!=='BMI323'||p?.interface!=='SPI'||p.accelLsbPerG!==2048||p.gyroLsbPerDps!==16.384||p.temperatureLsbPerC!==512||p.temperatureOffsetC!==23||p.accConfig!==0x70b9||p.gyroConfig!==0x70c9)throw new Error('Unsupported IMU schema/profile. Update both boards; legacy MPU data must not be decoded as BMI323.');
 return p;
}
export function boardCommunication(imu={},link={},stale=false){
 if(stale)return {title:'Status unavailable',tone:'neutral',description:'The dashboard has not received a recent update. Stay connected to the vest to confirm the board connection.'};
 if(link.enabled===false)return {title:'Communication disabled',tone:'warn',description:'Communication with the Sensing board is disabled in Main settings.'};
 if(link.online!==true)return {title:'Not communicating',tone:'bad',description:'Main is not receiving updates from the Sensing board. Check that both boards are powered and their connecting wires are secure.'};
 if(link.twoWayConfirmed===true||imu.online===true)return {title:'Communicating',tone:'good',description:'Main and Sensing are exchanging updates. '+(imu.healthy===false?'The board connection is working, but the motion sensor needs attention.':'The board connection is working normally.')};
 return {title:'Connection incomplete',tone:'warn',description:'Main receives Sensing status, but two-way data exchange is not yet confirmed. Check that both boards use matching firmware.'};
}
