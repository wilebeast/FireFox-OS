/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 30000;

SpecialPowers.addPermission("mobileconnection", true, document);

let icc = navigator.mozMobileConnection.icc;
ok(icc instanceof MozIccManager, "icc is instanceof " + icc.constructor);

function testDisplayTextGsm7BitEncoding(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_DISPLAY_TEXT);
  is(cmd.options.userClear, true);
  is(cmd.options.text, "Saldo 2.04 E. Validez 20/05/13. ");

  runNextTest();
}

function testLocalInfoLocation(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_PROVIDE_LOCAL_INFO);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, icc.STK_LOCAL_INFO_LOCATION_INFO);
  is(cmd.options.localInfoType, icc.STK_LOCAL_INFO_LOCATION_INFO);

  runNextTest();
}

function testLocalInfoImei(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_PROVIDE_LOCAL_INFO);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, icc.STK_LOCAL_INFO_IMEI);
  is(cmd.options.localInfoType, icc.STK_LOCAL_INFO_IMEI);

  runNextTest();
}

function testLocalInfoDate(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_PROVIDE_LOCAL_INFO);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, icc.STK_LOCAL_INFO_DATE_TIME_ZONE);
  is(cmd.options.localInfoType, icc.STK_LOCAL_INFO_DATE_TIME_ZONE);

  runNextTest();
}

function testLocalInfoLanguage(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_PROVIDE_LOCAL_INFO);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, icc.STK_LOCAL_INFO_LANGUAGE);
  is(cmd.options.localInfoType, icc.STK_LOCAL_INFO_LANGUAGE);

  runNextTest();
}

function testTimerManagementStart(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_TIMER_MANAGEMENT);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, icc.STK_TIMER_START);
  is(cmd.options.timerAction, icc.STK_TIMER_START);
  is(cmd.options.timerId, 0x01);
  is(cmd.options.timerValue, (0x01 * 60 * 60) + (0x02 * 60) + 0x03);

  runNextTest();
}

function testTimerManagementDeactivate(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_TIMER_MANAGEMENT);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, icc.STK_TIMER_DEACTIVATE);
  is(cmd.options.timerAction, icc.STK_TIMER_DEACTIVATE);
  is(cmd.options.timerId, 0x04);

  runNextTest();
}

function testTimerManagementGetCurrentValue(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_TIMER_MANAGEMENT);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, icc.STK_TIMER_GET_CURRENT_VALUE);
  is(cmd.options.timerAction, icc.STK_TIMER_GET_CURRENT_VALUE);
  is(cmd.options.timerId, 0x08);

  runNextTest();
}

function testSetupCall(cmd) {
  log("STK CMD " + JSON.stringify(cmd));
  is(cmd.typeOfCommand, icc.STK_CMD_SET_UP_CALL);
  is(cmd.commandNumber, 0x01);
  is(cmd.commandQualifier, 0x04);
  is(cmd.options.address, "012340123456,1,2");
  is(cmd.options.confirmMessage, "Disconnect");
  is(cmd.options.callMessage, "Message");

  runNextTest();
}

let tests = [
  {command: "d0288103012180820281020d1d00d3309bfc06c95c301aa8e80259c3ec34b9ac07c9602f58ed159bb940",
   func: testDisplayTextGsm7BitEncoding},
  {command: "d009810301260082028182",
   func: testLocalInfoLocation},
  {command: "d009810301260182028182",
   func: testLocalInfoImei},
  {command: "d009810301260382028182",
   func: testLocalInfoDate},
  {command: "d009810301260482028182",
   func: testLocalInfoLanguage},
  {command: "d011810301270082028182a40101a503102030",
   func: testTimerManagementStart},
  {command: "d00c810301270182028182a40104",
   func: testTimerManagementDeactivate},
  {command: "d00c810301270282028182a40108",
   func: testTimerManagementGetCurrentValue},
  {command: "d029810301100482028182050a446973636f6e6e6563748609811032042143651c2c05074d657373616765",
   func: testSetupCall},
];

let pendingEmulatorCmdCount = 0;
function sendStkPduToEmulator(cmd, func) {
  ++pendingEmulatorCmdCount;

  runEmulatorCmd(cmd, function (result) {
    --pendingEmulatorCmdCount;
    is(result[0], "OK");
  });

  icc.onstkcommand = function (evt) {
    func(evt.command);
  }
}

function runNextTest() {
  let test = tests.pop();
  if (!test) {
    cleanUp();
    return;
  }

  let cmd = "stk pdu " + test.command;
  sendStkPduToEmulator(cmd, test.func)
}

function cleanUp() {
  if (pendingEmulatorCmdCount) {
    window.setTimeout(cleanUp, 100);
    return;
  }

  SpecialPowers.removePermission("mobileconnection", document);
  finish();
}

runNextTest();
