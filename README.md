# SmartNest Supabase-connected website

## Included
- `index.html`: login, 8 device switches, gate Open/Close/Stop, temperature/humidity display, Supabase Realtime.
- `supabase_extra_setup.sql`: adds a distinct `stopped` gate state and authenticated sensor INSERT policy.

## Project connection
The supplied project ID was used to form the Supabase URL:
`https://ojqghjtjotsmrxwmjjkj.supabase.co`
The supplied `sb_publishable_...` key is included in the frontend. Supabase publishable keys are intended for client use; RLS must remain enabled.

## Install
1. Replace the existing repository `index.html` with this file.
2. Commit to GitHub `main`.
3. Enable GitHub Pages from Settings → Pages → Deploy from branch → `main` → `/(root)`.
4. Open the Pages URL and sign in with the existing Supabase Auth email/password.

## Additional SQL
Run `supabase_extra_setup.sql` once in Supabase SQL Editor. If it reports that the policy already exists, do not create a duplicate.

## Required database tables
- `device_states(device_id, is_on, updated_at)` with the eight device IDs already seeded.
- `gate_state(id, state, updated_at)`, id 1.
- `sensor_readings(id, temperature, humidity, recorded_at)`.

## ESP32 work still required
The web dashboard writes desired states to Supabase and listens for realtime updates. ESP32 firmware still needs to authenticate using a separate dedicated device account, read/subscribe to `device_states` and `gate_state`, operate relays/motor driver, stop the gate using limit switches, and insert DHT22 readings into `sensor_readings`.

Do not put a Supabase secret/service-role key or database password in this public repository. Keep RLS enabled. Current example policies allow all authenticated project users to control the model; restrict policies to specific identities before public use. Do not expose hardware control endpoints publicly.
