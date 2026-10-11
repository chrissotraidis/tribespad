// TribesPad reference trace: identical offline physics scenario for the original
// Windows game and TribesPad. Append to base/autoexec.cs (keep a backup).
// Hosts Katabatic CTF without bots, spawns, places the player at a fixed point
// with a fixed velocity and logs server-side state; then quits.
setLogMode(1);
$TPT::state = 0;
function TPT_log(%m){ echo("TPTRACE " @ %m); }
function TPT_start(){ TPT_log("host"); $Host::BotsEnabled = 0; $Host::BotCount = 0; $Host::Map = "Katabatic"; $Host::MissionType = "CTF"; LaunchGame("Host"); schedule(2000,0,"TPT_go"); }
function TPT_go(){ $Host::BotsEnabled = 0; $Host::BotCount = 0; tryToStartHostedGame(); schedule(1000,0,"TPT_wait"); }
function TPT_player(){ for(%i=0;%i<ClientGroup.getCount();%i++){%c=ClientGroup.getObject(%i);if(!%c.isAIControlled())return %c.player;} return 0; }
function TPT_wait(){
  %p = TPT_player();
  if (!isObject(%p)) { if (isObject(ServerConnection)) { mouseFire(1); mouseFire(0); } schedule(1000,0,"TPT_wait"); return; }
  if (!$MatchStarted) { schedule(500,0,"TPT_wait"); return; }
  schedule(3000,0,"TPT_place");
}
function TPT_place(){
  %p = TPT_player(); if (!isObject(%p)) { schedule(1000,0,"TPT_wait"); return; }
  %h = getTerrainHeight("250 -100");
  TPT_log("terrain " @ %h @ " mission " @ $CurrentMission @ " armor " @ %p.getDataBlock().getName());
  %p.setTransform("250 -100 " @ (%h + 12) @ " 0 0 1 0");
  %p.setVelocity("14 6 0");
  %p.setEnergyLevel(60);
  $TPT::t0 = getSimTime(); $TPT::last = ""; $TPT::n = 0;
  TPT_sample();
}
function TPT_sample(){
  %p = TPT_player(); if (!isObject(%p)) { TPT_log("lost player"); return; }
  %s = %p.getPosition() @ " | " @ %p.getVelocity() @ " | " @ %p.getEnergyLevel() @ " | " @ %p.getDamageLevel();
  if (%s !$= $TPT::last) { $TPT::n++; TPT_log("s " @ $TPT::n @ " " @ (getSimTime() - $TPT::t0) @ " " @ %s); $TPT::last = %s; }
  if (getSimTime() - $TPT::t0 < 8000) schedule(4,0,"TPT_sample"); else { TPT_log("done " @ $TPT::n); schedule(1000,0,"quit"); }
}
schedule(20000, 0, "TPT_start");
