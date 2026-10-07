# Multiplayer

Beta: in progress. Dungeon Siege II's multiplayer screens are switched
off in the GOG 2.3 release; this port switches them back on and connects them to a replacement for GameSpy, the
online service the game was built on (shut down in 2014).

## What works so far

- **Main menu → Multiplayer** opens the game's own screens (Local Network, Internet, GameSpy).
- **LAN games:** Local Network → a nickname → the LAN lobby. Players on the same network find each other there,
  create or import a party, and one of them hosts a game; the others join its staging area, mark Ready, and the host
  starts it. Everyone loads into the same world and plays together. **A LAN game needs no CD key and no internet
  connection** (no OpenSpy either): the lobby runs on the players' own machines.
- **Parties:** created or imported in the lobby, they're saved under `Save/MultiPlayer/<name>`. Importing copies a
  single-player hero there and leaves the single-player save as it was.
- **The online lobby:** Internet signs in to [OpenSpy](https://openspy.net), the community-run GameSpy replacement,
  and shows its party selection, game list and chat rooms. It needs a CD key (below).
- **Tested** with two copies of the game on one Mac (lobby, hosting, joining, the intro, playing in the same area).
  **Not tested yet:** two Macs, play between a Mac and a PC, internet games.

### How the port makes LAN games work

The GOG 2.3 release shipped with multiplayer switched off, and its LAN path had never worked. The port fixes three
things in it:

- **The DirectPlay thread pool.** The game creates one before its DirectPlay client and server; it was missing, and
  Host Game froze behind an error message the game never showed.
- **A multiplayer world session.** The game makes its world session multiplayer only on the way back from a game into
  the lobby, never on the first visit from the main menu. With a single-player session a hosted game loaded with the
  single-player rules: Start did nothing (no player had a start position), the host never finished syncing with the
  others, kept its game locked, and nobody could join. The port gives the lobby a multiplayer session the way the game
  does itself (a hook on its frontend state change).
- **Addresses.** The game gives out the first address it finds for the own machine; the port lists the LAN ones first
  (the router's public address last).

### Known issues

- After leaving the lobby for a single-player game, the lobby's Party Selection window stays drawn over the top left
  of the screen (it does with the port's session fix switched off too).
- The intro movie plays on every machine when a game starts; Esc skips it.
- Two copies on one Mac can't ping each other (both want GameSpy's ping port, 13139), so the connection bars in the
  staging area stay red there. Separate machines don't have this.

## Your CD key

The online lobby needs a CD key: the 25-character product key of an **original retail copy** of Dungeon Siege II (the
sticker under disc 1 in the box). The game doesn't read the key itself but a hash of it that the retail installer used
to write; the port computes the same hash from your key. Only that hash is stored, never the key.

**The Steam and GOG releases come without a CD key.** Both sell Dungeon Siege II as a single-player game (GameSpy had
shut down) and give out no in-game key; the keys sold for them are store activation codes, which the game doesn't
accept. Single-player is unaffected. For now, online play needs an original retail key; this port doesn't work around
the key check. A key for every buyer would have to come from the rights holder.

**Each copy needs its own key.** The same key can't be in the lobby from two computers at once, as with GameSpy.

- **On a Mac (this app):** in the launch window, press **Online** and enter the key. Or, from the repository:
  `./install.sh --cd-key`. A first install also offers it at the end.
- **For a Windows PC** (or Wine/Proton on Linux) **without the retail installer:** on any Mac with this repository,
  `./install.sh --cd-key --reg-file DS2-cdkey.reg` with that PC's own key, then on the PC double-click the `.reg`
  file to import it. (A PC also needs the OpenSpy client to reach the lobby.)

The hash is kept in the data folder as `cdkey-hash.txt` (readable only by you); remove it there, or with **Remove
Key** in the launch window.

## Settings

- `DS2_LOBBY=off`: don't redirect the game's GameSpy addresses to OpenSpy.
- `DS2_NO_MULTIPLAYER=1`: keep the Multiplayer button disabled, as the GOG release ships.
- `DS2_NO_MP_SESSION=1`: keep the game's single-player world session in the multiplayer lobby (LAN games then don't start).
- `DS_NO_PORTMAP=1`: don't ask the router to forward ports when hosting.

## Privacy

Going online sends your nickname, your IP address and your game listings to OpenSpy's servers, as the game once did
to GameSpy. Your CD key hash is sent to the lobby, as the original game did. LAN games stay on your network.
