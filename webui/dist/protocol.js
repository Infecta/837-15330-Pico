export const VID=0x0ca3, PID=0x0021, USAGE_PAGE=0xff60, USAGE=1, REPORT_ID=1, REPORT_SIZE=63;
export const COMMAND={subscribe:1,brightness:2,calibrate:3,save:4,trigger:5,channelSensitivity:6,channelThreshold:7};
export function encodeCommand(command,sequence,value=0,channel=0){
 if(!Object.values(COMMAND).includes(command)||!Number.isInteger(sequence)||sequence<1||sequence>65535)throw new Error('Invalid control command');
 if(command===COMMAND.brightness&&(!Number.isInteger(value)||value<0||value>100))throw new Error('Brightness must be 0–100');
 if(command===COMMAND.trigger&&(!Number.isInteger(value)||value<1||value>99))throw new Error('IR trigger must be 1–99%');
 if([6,7].includes(command)&&(!Number.isInteger(channel)||channel<0||channel>5||!Number.isInteger(value)||value<(command===6?1:0)||value>(command===6?99:4094)))throw new Error('Invalid channel tuning');
 const bytes=new Uint8Array(REPORT_SIZE);const v=new DataView(bytes.buffer);bytes[0]=1;bytes[1]=command;v.setUint16(2,sequence,true);bytes[4]=value;if([6,7].includes(command)){bytes[4]=channel;v.setUint16(5,value,true);}return bytes;
}
export function decodeReport(view){
 if(!(view instanceof DataView)||view.byteLength!==REPORT_SIZE||view.getUint8(0)!==1)throw new Error('Unsupported controller report. Install the WebHID firmware.');
 const cal=view.getUint8(1),brightness=view.getUint8(3);if(cal>3||brightness>100)throw new Error('Invalid controller state');
 const channels=Array.from({length:6},(_,i)=>({raw:view.getUint16(14+i*2,true),baseline:view.getUint16(26+i*2,true),trigger:view.getUint16(38+i*2,true),release:view.getUint16(50+i*2,true),absolute:!!(view.getUint8(11)&1<<i),blocked:!!(view.getUint8(2)&1<<i)}));
 if(channels.some(c=>Object.values(c).some(v=>typeof v==='number'&&v>4095)||c.trigger>c.baseline||c.release>c.baseline||c.trigger>c.release))throw new Error('Invalid sensor readings');
 return {perChannel:!!(view.getUint8(11)&128),version:1,calibration:cal,brightness,flags:view.getUint8(4),triggerPercent:view.getUint8(5),hysteresis:view.getUint8(6),command:view.getUint8(7),sequence:view.getUint16(8,true),result:view.getUint8(10),sampleId:view.getUint16(12,true),powerCap:view.getUint8(62),channels};
}
export function isController(device){return device.vendorId===VID&&device.productId===PID&&device.collections.some(c=>c.usagePage===USAGE_PAGE&&c.usage===USAGE);}
