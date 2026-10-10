// TribesPad Simulator playtest. Append to the app container base/autoexec.cs (keep a backup); not shipped.
// Drives the original control functions that key bindings call; ends the match through the original time-limit path.
setLogMode(1);
$TPPlay::t = 0;
function TPPlay_log(%m){ echo("TPPLAY " @ getSimTime() @ " " @ %m); }
function TPPlay_start(){ TPPlay_log("launch host"); LaunchGame("Host"); schedule(1500,0,"TPPlay_host"); }
function TPPlay_host(){ TPPlay_log("start hosted game"); tryToStartHostedGame(); schedule(2000,0,"TPPlay_tick"); }
function TPPlay_quit(){ TPPlay_log("quit"); quit(); }
function TPPlay_tick(){
  %c = isObject(ServerConnection) ? ServerConnection.getControlObject() : 0;
  if (isObject(%c)) $TPPlay::t++; else { schedule(1000,0,"TPPlay_tick"); return; }
  if ($TPPlay::t % 5 == 0) TPPlay_log("t=" @ $TPPlay::t SPC $CurrentMission SPC %c.getTransform());
  if ($TPPlay::t < 25 && $TPPlay::t % 3 == 0) { mouseFire(1); mouseFire(0); }
  if ($TPPlay::t == 25) { TPPlay_log("move"); moveforward(1); }
  if ($TPPlay::t > 25) {
    $mvYaw = 0.03;
    if ($TPPlay::t % 4 == 0) { mouseFire(1); mouseFire(0); }
    if ($TPPlay::t % 8 == 0) mouseJet(1);
    if ($TPPlay::t % 8 == 3) mouseJet(0);
    if ($TPPlay::t % 7 == 0) { jump(1); jump(0); }
  }
  if ($TPPlay::t == 60) { TPPlay_log("time limit reached on " @ $CurrentMission); Game.timeLimitReached(); }
  if ($TPPlay::t == 110) { TPPlay_log("disconnect"); disconnect(); schedule(5000,0,"TPPlay_quit"); return; }
  schedule(1000,0,"TPPlay_tick");
}
schedule(25000, 0, "TPPlay_start");
