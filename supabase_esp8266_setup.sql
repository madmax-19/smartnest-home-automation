-- Run once in Supabase SQL Editor.
-- ESP8266 online/offline heartbeat table.

CREATE TABLE IF NOT EXISTS public.device_heartbeat (
  device_id text PRIMARY KEY,
  last_seen timestamptz NOT NULL DEFAULT now()
);

ALTER TABLE public.device_heartbeat ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS "Authenticated device can read heartbeat" ON public.device_heartbeat;
CREATE POLICY "Authenticated device can read heartbeat"
ON public.device_heartbeat FOR SELECT TO authenticated USING (true);

DROP POLICY IF EXISTS "Authenticated device can insert heartbeat" ON public.device_heartbeat;
CREATE POLICY "Authenticated device can insert heartbeat"
ON public.device_heartbeat FOR INSERT TO authenticated WITH CHECK (true);

DROP POLICY IF EXISTS "Authenticated device can update heartbeat" ON public.device_heartbeat;
CREATE POLICY "Authenticated device can update heartbeat"
ON public.device_heartbeat FOR UPDATE TO authenticated USING (true) WITH CHECK (true);

-- Ensure the device account can read the existing device states table.
-- Keep your existing device_states policies if they already grant this access.
