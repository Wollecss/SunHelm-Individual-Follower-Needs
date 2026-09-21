Scriptname SunHelmFollowerNeeds Native Hidden

; Native bindings into SunHelmFollowerNeeds.dll.
;
; This script contains no logic and never runs on a timer - it exists only so Papyrus has something
; to attach the natives to. The mod itself is entirely native C++; nothing here is on the critical
; path of tracking follower needs, and a modlist without this file loses nothing except the ability
; for other mods to ask about followers.
;
; It is here for the CHIM bridge. The DLL queues a short payload whenever a tracked follower's
; condition changes in a way worth reporting, and the bridge script drains that queue and forwards
; each entry to HerikaServer. Keeping the CHIM specifics out of the DLL means the bridge can follow
; CHIM's protocol as it changes without this plugin needing a rebuild.
;
; Typical use:
;
;   int pending = SunHelmFollowerNeeds.GetPendingCount()
;   while pending > 0
;       string npc = SunHelmFollowerNeeds.PeekPendingActor()
;       string payload = SunHelmFollowerNeeds.ConsumePending()
;       if npc != "" && payload != ""
;           AIAgentFunctions.logMessageForActor(payload, "infoaction", npc)
;       endif
;       pending -= 1
;   endwhile
;
; If the DLL is missing the calls return 0 and "", so a consumer degrades to doing nothing rather
; than erroring.

; How many follower updates are waiting. Zero on most polls: payloads are only queued when a
; follower's reported condition actually changes, not every tick.
int Function GetPendingCount() global native

; The name of the follower the next payload describes, without removing it from the queue. Needed
; separately because CHIM routes messages by actor name.
string Function PeekPendingActor() global native

; Removes and returns the next payload. Returns "" when the queue is empty.
;
; Payload grammar, matching plugin/ext/sunhelm_needs/lib/sunhelm_followers.php in the bridge:
;   sunhelm_follower@<Name>@<hunger 0-5>@<thirst 0-5>@<drunk 0|1>@<drinks>@<diseased 0|1>@<gold>
;   sunhelm_follower_gone@<Name>
string Function ConsumePending() global native
