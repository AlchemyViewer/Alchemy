# Custom RLV Additions Documentation

This document describes the syntax, behavior, and effects of custom Restricted Love (RLV) commands integrated into this viewer.

## 1. Contact Lists & Profiles Blocking

### Commands and Targets

#### `@showfriends=n|y`
- **Target**: Friends list and Recent contacts list.
- **Behavior & Effects**: 
  - Clears friends lists (both online and offline lists) and the recent contacts list inside the "People" panel.
  - Visually disables (grays out) the "Friends" and "Recent" tabs inside the "People" panel.
  - Does NOT block initiating IM sessions (users can still start IMs through other means).
- **Notification**: `RLVFriendsBlocked` ("Your friends list is blocked by your RLV restrictions").

#### `@showprofiles=n|y`
- **Target**: Avatar Profiles.
- **Behavior & Effects**:
  - Intercepts and blocks attempts to view other residents' Profiles (standard, legacy, and web profile floaters).
  - Your own profile remains accessible.
- **Notification**: `RLVProfileBlocked` ("Profiles are blocked by your RLV restrictions").

---

## 2. Viewer Shutdown (@shutdown)

### Syntax
- `@shutdown=force` - Initiates viewer shutdown.

### Behavior & Effects
- Displays a warning notification with a countdown timer to the user, and then cleanly shuts down the viewer.

---

## 3. UI Hiding (@hideui)

### Syntax
- `@hideui=n` - Hides the user interface elements.
- `@hideui=y` - Restores the user interface elements.

### Behavior & Effects
- Hides the main menu bar, toolbars, and floaters (except for IM session containers and nearby chat to allow communication).
- Restores standard UI visibility when released.

---

## 4. Tab and Floater Access Restrictions

These commands block access to specific viewer panels/floaters. 
When set to `n` (restricting access):
- The corresponding target floater(s) are immediately closed if open.
- Future attempts to open the target floater(s) are blocked.
- A customized RLV blocked notification is displayed to the user.
When set to `y` (removing the restriction):
- Access is restored, allowing the floaters to be opened normally.

### Commands and Targets

#### `@showsearch=n|y`
- **Target**: Search (`"search"` floater).
- **Notification**: `RLVSearchBlocked` ("Search is blocked by your RLV restrictions").

#### `@showdevelop=n|y`
- **Target**: Advanced and Developer top menu bars.
- **Notification**: None (menus are hidden).

#### `@showconversation=n|y`
- **Target**: Conversation log history (`"conversation"` floater) AND active IM container (`"im_container"` floater).
- **Notification**: `RLVConversationBlocked` ("Conversation history is blocked by your RLV restrictions").

#### `@showoutfits=n|y`
- **Target**: Outfits/Appearance (`"appearance"` floater).
- **Notification**: `RLVAppearanceBlocked` ("Appearance is blocked by your RLV restrictions").

#### `@showbuild=n|y`
- **Target**: Edit Tools (`"build"` floater).
- **Notification**: `RLVBuildBlocked` ("Edit Tools is blocked by your RLV restrictions").

#### `@showdestinations=n|y`
- **Target**: Destinations Guide (`"destinations"` floater).
- **Notification**: `RLVDestinationsBlocked` ("Destinations Guide is blocked by your RLV restrictions").

#### `@showcamera=n|y`
- **Target**: Camera Controls (`"camera"` and `"camera_presets"` floaters).
- **Notification**: `RLVCameraBlocked` ("Camera Controls are blocked by your RLV restrictions").

#### `@showpreferences=n|y`
- **Target**: Preferences (`"preferences"` floater).
- **Notification**: `RLVPreferencesBlocked` ("Preferences are blocked by your RLV restrictions").

---

## 4b. View Restrictions

#### `@lockmouselook=y|n`
- **Syntax**: `@lockmouselook=y` (Force and lock into mouselook), `@lockmouselook=n` (Release).
- **Behavior & Effects**: Forces the viewer into mouselook and strictly prevents the user from exiting it (e.g., by scrolling to un-zoom or pressing ESC).

---

## 5. E-Stim & PawPrint Sensor Integration

These commands configure the native integration with the DG-Lab Coyote e-stim device and PawPrint BLE sensors via the local companion bridge daemon.

### Basic E-Stim Control Commands

#### `@estim_intensity:<channel>:<0-99>=force`
- **Effect**: Immediately sets the stimulation intensity of FMOD Studio / Coyote channel `<channel>` (`a` or `b`) to a value between `0` (off) and `99` (maximum). Capped by the global max output settings.

#### `@estim_shock:<channel>:<intensity>:<hz>:<ms>=force`
- **Effect**: Immediately delegates a precise, timed stimulation pulse to the daemon (e.g. `@estim_shock:a:35:60:500=force`). This completely avoids script/sim lag associated with sending discrete ON and OFF `@estim_intensity` commands. Overlapping commands are queued and played sequentially.

#### `@estim_freq:<channel>:<hz>=force`
- **Effect**: Sets the stimulation frequency in Hz (typically `10` to `150`) for the specified `<channel>`.

#### `@estim_pulse_width:<channel>:<µs>=force`
- **Effect**: Configures the pulse width in microseconds (up to `150` on Coyote V3) for the specified `<channel>`.

#### `@estim_waveform:<channel>:<sine/square/tri/trap>=force`
- **Effect**: Toggles the output pulse shape waveform for the specified `<channel>`.

#### `@estim_pattern:<channel>:<id>=force`
- **Effect**: Triggers one of the device's built-in wave patterns on `<channel>`.

#### `@estim_audio_sync:<on/off>=force`
- **Effect**: Enables or disables FMOD-based real-time audio-to-estim mapping, analyzing bass peaks to modulate Channel A and mid-range sounds to modulate Channel B frequency.

#### `@estim_stop=force`
- **Effect**: Emergency panic halt; immediately cuts off stimulation power on all channels.

---

### PawPrint BLE Sensor Commands

#### `@estim_sensor_mode:<sensor>:<button/angle/accel/all>=force`
- **Effect**: Configures the reporting mode for a connected PawPrint sensor.

#### `@estim_trigger:add:<sensor>:<axis>:<op>:<val>:<action>=force`
- **Effect**: Registers a local rule that evaluates incoming sensor telemetry.
  - **Parameters**:
    - `<sensor>`: Sensor ID (e.g. `paw`)
    - `<axis>`: Target axis (`pitch`, `roll`, `yaw`, `accel_x`, `accel_y`, `accel_z`, `button`)
    - `<op>`: Relational operator (`>`, `<`, `==`)
    - `<val>`: Float threshold value (e.g. `60.0`, `1.0`)
    - `<action>`: Re-routed action, which can be an e-stim write command (e.g. `intensity:a:45`) or an LSL chat shout (`lsl:<channel>:<message>`).
  - **Evaluation (Edge-Triggered)**: To prevent BLE bandwidth congestion and LSL chat queue spamming, trigger rules are edge-triggered. The action executes exactly once when the condition transitions from `False` to `True`. The state resets when the condition returns to `False`.

#### `@estim_trigger:clear=force`
- **Effect**: Clears all active registered sensor trigger rules.

---

### LSL Sensor Integration & Telemetry

These query and notification commands enable in-world LSL scripts to poll sensor positions or subscribe to change events.

#### Direct Query Command
* **Syntax**: `@estim_getval:<sensor>:<axis>=<channel>`
* **Description**: Queries the viewer's cache for the current telemetry value of `<sensor>`'s `<axis>`.
* **Reply**: The viewer sends a chat reply back on `<channel>` containing the formatted value (e.g. `45.000000` or `1` for buttons).

#### Real-time Notification Subscription
* **Syntax**: `@estim_notify:<sensor>:<axis>:<channel>=n|y`
* **Description**: Subscribes (`=n`) or unsubscribes (`=y`) to change notifications for `<sensor>`'s `<axis>`.
* **Reply**: Whenever the target axis value changes, the viewer shouts a message formatted as `sensor:axis=value` (e.g. `paw:x=45.000000` or `paw:button=1.000000`) on the specified `<channel>`.
* **Lifecycle**: Managed dynamically by the standard RLV object lifetime; when the object containing the script is detached, reset, or goes out of range, all associated notifications are automatically cleared.

---

## 6. LSL RLV Dialog System

The **LSL RLV Dialog System** is a modular, multi-script system designed for Second Life / OpenSim attachments to enforce and manage RLV restriction states with persistence, proximity tracking, and access levels.

### 6.1 Multi-Script Architecture
To maintain low script memory usage, the system is split into:
1. **Core Controller (`rlv_core.lsl`)**: Manages RLV state validation, configuration parsing, proximity calculations, safety notifications, and persistence.
2. **Menu Interface (`rlv_menu.lsl`)**: Handles interactive dialog menus, access checks, text box text inputs, and nearby avatar sensors.

### 6.2 Configuration Notecard (`rlv_config`)
The Core script reads a notecard named `rlv_config` from the object's inventory on initialization/reload.
- **Syntax**: `RestrictionName|RLV_Command|DefaultState` (e.g. `Search|showsearch|y`).
- **Default Fields**:
  - `Search|showsearch|y`
  - `Friends|showfriends|y`
  - `Profiles|showprofiles|y`
  - `Developer|showdevelop|y`
  - `Conversation|showconversation|y`
  - `Outfits|showoutfits|y`
  - `Build|showbuild|y`
  - `Destinations|showdestinations|y`
  - `Camera|showcamera|y`
  - `Preferences|showpreferences|y`
  - `Mouselook|lockmouselook|n`

### 6.3 Link Message Protocol
Scripts communicate via standard link messages using the following code ranges:
- **`100` (`LINK_RELOAD_CONFIG`)**: Triggers Core to reload the configuration notecard.
- **`101` (`LINK_TOGGLE_RESTRICTION`)**: Toggles a restriction state. String format: `Index|NewState` (e.g. `0|1` to enforce).
- **`102` (`LINK_UPDATE_LOCKOUT`)**: Configures wearer lockout mode (`"1"` or `"0"`).
- **`103` (`LINK_UPDATE_HARDCORE`)**: Configures hardcore mode (`"1"` or `"0"`).
- **`104` (`LINK_UPDATE_SAFEWORD`)**: Updates the safeword string.
- **`105` (`LINK_TRIGGER_SAFEWORD`)**: Instantly clears all RLV restrictions and unlocks settings.
- **`106` (`LINK_UPDATE_PROXIMITY`)**: Saves proximity trigger parameters. String format: `Index|ConditionType|Distance|State` (e.g. `2|CLOSE_OWNER|10.0|1`).
- **`107` / `108` (`LINK_ADD_OWNER` / `LINK_REMOVE_OWNER`)**: Adds or removes an owner UUID.
- **`109` / `110` (`LINK_ADD_WHITELIST` / `LINK_REMOVE_WHITELIST`)**: Adds or removes a whitelist member UUID.
- **`112` (`LINK_CLEAR_ALL_RESTRICTIONS`)**: Clears all active restrictions and disables all proximity triggers.
- **`113` (`LINK_UPDATE_DEVICE_LOCK`)**: Toggles device lock state (`"1"` or `"0"`) to lock the attachment on the wearer.
- **`140` - `147` (`LINK_ESTIM_SPANK_CFG`)**: Configures settings and triggers for Coyote E-Stim Spank Capture Channel A and B.
- **`200` (`LINK_SEND_MENU`)**: Request/Response protocol for serializing internal states between scripts. String format: `Lockout|Hardcore|Safeword|OwnersCsv|WhitelistCsv|RestrictionsCsv|ProximityCsv|DeviceLock`.

### 6.4 Key Features & Behavior
- **Unified & Paginated Restrictions Menu**: Merges restriction toggle states and proximity triggers into a single paginated list (up to 8 per page) with loop-around logic and a custom 12-button navigation layout.
- **Dedicated Sub-menus**: Displays individual restriction setting screens with options to manually toggle the restriction state, configure proximity settings (state, type, distance), and return directly to the caller page of the restrictions list.
- **Device Lock**: Restricts wearer from detaching the item (enforcing `@detach=n`) when device lock is active, which can be toggled via the Main Menu by authorized users or owner.
- **Chat Command Listener**: Supports opening the menu via `/100 mamenu` for authorized users.
- **Access Control branching**: Handlers verify users against Owners, Whitelist, and Wearer hierarchy. Public access is rejected.
- **Wearer Lockout**: Wearer is prevented from editing access lists, settings, or proximity triggers.
- **Hardcore Mode**: Wearer is locked out of all menus except the Safeword input box. Entering the correct safeword clears all restrictions.
- **Remote Shutdown Button**: Available in the Settings menu exclusively to Owners while Hardcore mode is active. Triggers the `@shutdown=force` RLV command to forcibly terminate the viewer.
- **Proximity Tracking**: Core script evaluates `llGetObjectDetails` on owner/whitelist targets region-wide every 5 seconds. If targets are within the configured distance, restrictions are dynamically auto-enforced/auto-released.
- **Sensor scans**: Features automatic name truncation to 24 characters (due to LSL dialog button limits) and parallel tracking of UUIDs.
- **State Persistence**: Uses Linkset Data (LSD) to persist owners, whitelist, locks, device lock, and restrictions across sim crossings, detachments, and script crashes.

### 6.5 Coyote E-Stim Spank Capture Integration
The system integrates two independent Coyote E-Stim Spank Capture modules (Channel A and Channel B) directly into the core engine and menu system.

#### Settings Keys in LinksetData (LSD)
- `lsd:spank:a:enabled` / `lsd:spank:b:enabled`: Enabled status ("1" or "0").
- `lsd:spank:a:channel` / `lsd:spank:b:channel`: LSL chat channel to listen on (e.g. "0" for local chat).
- `lsd:spank:a:intensity` / `lsd:spank:b:intensity`: Stimulation intensity value between 0 and 255 (default "80").
- `lsd:spank:a:freq` / `lsd:spank:b:freq`: Stimulation frequency in Hz (typically 10 to 1000 Hz, default "50").
- `lsd:spank:a:dur` / `lsd:spank:b:dur`: Stimulation duration in ms (typically 100 to 5000 ms, default "300").
- `lsd:spank:a:triggers` / `lsd:spank:b:triggers`: Pipe-separated (`|`) list of trigger phrases (e.g. `spanks ℳαɾgαєɾу Ⴘαɾα's ass.`).

#### Link Message Protocol Extensions (range 140-147)
- **`140`**: Toggle enabled state. Format: `Chan|State` (e.g. `a|1`).
- **`141`**: Set intensity. Format: `Chan|Value` (e.g. `a|120`).
- **`142`**: Set frequency. Format: `Chan|Value` (e.g. `a|60`).
- **`143`**: Set duration. Format: `Chan|Value` (e.g. `a|500`).
- **`144`**: Set listen channel. Format: `Chan|Value` (e.g. `a|0`).
- **`145`**: Add trigger phrase. Format: `Chan|Phrase` (e.g. `a|some phrase`).
- **`146`**: Remove trigger phrase. Format: `Chan|Phrase` (e.g. `a|some phrase`).
- **`147`**: Clear all trigger phrases. Format: `Chan` (e.g. `a`).

---

## 7. RLV Folder Monitor System

The **RLV Folder Monitor** (`rlv_folder_monitor.lsl`) is a standalone utility script that periodically polls RLV worn folder states under a configured root path (default: `~EasyDetatch`), parses the subfolder statuses, and applies custom attachment synchronization rules persisted in Linkset Data (LSD).

### 7.1 Polling & Response Parsing
- **Scan Trigger**: Queries RLV active worn states using `@getinvworn:<root_path>=<channel>`.
- **Response Parser**: Handles the comma-separated viewer response (`|XX,Folder A|XX,Folder B|XX`). The script splits by `,`, ignores the first entry, and splits the remaining subfolder entries by `|` to separate the folder name from the 2-digit wear status code.
- **Anti-Spam State Tracking**: Maintains a list of the last scanned folder states and only triggers rules when a folder's binary wear state transitions (e.g. `worn <-> unworn`).

### 7.2 Rule Evaluation
Each rule is stored in LSD under `fm:rule:<id>` as `MonitoredFolder|ControlledFolder|ActionType`.
- **Detach on Wear (0)**: Wearing the monitored folder forces the controlled folder to detach. Removing the monitored folder forces the controlled folder to attach.
- **Attach on Wear (1)**: Wearing the monitored folder forces the controlled folder to attach. Removing the monitored folder forces the controlled folder to detach.

### 7.3 Menu Wizard & Pagination
A paginated, index-based dialog interface allows users to:
- Configure the RLV root path and polling interval (default: 3 seconds).
- View configured rules paginated 9 per page with navigation buttons (`<<`, `Back`, `>>`).
- Create rules using a wizard that displays scanned subfolders using page-based index labels (e.g., clicking `[1]`, `[2]`, `[3]` etc.), completely bypassing the 24-byte button character limit of LSL dialogs.
- Delete rules contiguously, where deleting a rule triggers automatic cleanup and defragmentation of LSD rule slots.

---

## 8. Word Triggers and Alert Notification Settings

The system includes a speech monitoring engine and a configurable alert notification system.

### 8.1 Word Trigger Engine
- **Mandatory/Forbidden Words**: Tracks speech rules and shocks the wearer on infractions.
- **Escalation Priority**: Evaluates infraction counters at Local or Global escalation levels with custom resets and overflow parameters.
- **Proximity Gating**: Bypasses speech rules when no owners are nearby.

### 8.2 Alert Notification Settings
The Settings menu includes an **Alert Settings** submenu which allows setting:
- **Verbose / Silent Mode**: Toggles whether acknowledgment messages describing configuration and state changes (restrictions, settings, triggers, spanks) are sent. Toggled via the `"Verbose"` button.
- **Wearer Messages**: Toggles whether the wearer receives these change acknowledgment alerts (while owners/operators always do). Toggled via the `"Wearer Msg"` button.
- **Hardcore Wearer Workaround**: The Wearer can always access the Settings menu to toggle Hardcore mode ON (even when locked or set as a non-owner/wearer), ensuring they can increase restriction level, while preventing modifications to Safewords or unlocking when locked.
