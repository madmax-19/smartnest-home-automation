-- Run once in Supabase SQL Editor.
-- Allows STOP to be represented separately from CLOSED.
ALTER TABLE public.gate_state DROP CONSTRAINT IF EXISTS gate_state_state_check;
ALTER TABLE public.gate_state ADD CONSTRAINT gate_state_state_check
CHECK (state IN ('closed','open','moving_open','moving_close','stopped'));

-- Allows an authenticated ESP32 account to upload DHT22 readings.
CREATE POLICY "Authenticated device can insert sensor readings"
ON public.sensor_readings FOR INSERT TO authenticated WITH CHECK (true);
