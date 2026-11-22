-- SmartNest roles: admin = full control, guest = view only, device = the ESP32 account.
-- Run once in Supabase > SQL Editor. Safe to run again.

create table if not exists public.user_roles (
  user_id uuid primary key references auth.users(id) on delete cascade,
  email text,
  role text not null default 'guest' check (role in ('admin','guest','device')),
  created_at timestamptz not null default now()
);
alter table public.user_roles enable row level security;

create or replace function public.app_role() returns text
language sql stable security definer set search_path = public as $$
  select role from public.user_roles where user_id = auth.uid();
$$;
grant execute on function public.app_role() to authenticated;

-- every new sign-up becomes a guest automatically
create or replace function public.handle_new_user() returns trigger
language plpgsql security definer set search_path = public as $$
begin
  insert into public.user_roles (user_id, email) values (new.id, new.email) on conflict (user_id) do nothing;
  return new;
end; $$;
drop trigger if exists on_auth_user_created on auth.users;
create trigger on_auth_user_created after insert on auth.users
  for each row execute function public.handle_new_user();

-- accounts that already exist (you and the ESP32 login) become admins so nothing stops working
insert into public.user_roles (user_id, email, role)
  select id, email, 'admin' from auth.users on conflict (user_id) do nothing;

-- who can read / change roles
drop policy if exists "read own role or admin reads all" on public.user_roles;
create policy "read own role or admin reads all" on public.user_roles for select to authenticated
  using (user_id = auth.uid() or public.app_role() = 'admin');
drop policy if exists "admin changes other users roles" on public.user_roles;
create policy "admin changes other users roles" on public.user_roles for update to authenticated
  using (public.app_role() = 'admin' and user_id <> auth.uid())
  with check (public.app_role() = 'admin' and user_id <> auth.uid());

-- replace the old "any signed-in user can write" policies on the data tables
do $$
declare t text; r record;
begin
  foreach t in array array['device_states','gate_state','sensor_readings','device_heartbeat'] loop
    execute format('alter table public.%I enable row level security', t);
    for r in select policyname from pg_policies where schemaname = 'public' and tablename = t loop
      execute format('drop policy %I on public.%I', r.policyname, t);
    end loop;
    execute format('create policy "signed-in users can read" on public.%I for select to authenticated using (true)', t);
    execute format('create policy "admin or device can insert" on public.%I for insert to authenticated with check (public.app_role() in (''admin'',''device''))', t);
    execute format('create policy "admin or device can update" on public.%I for update to authenticated using (public.app_role() in (''admin'',''device'')) with check (public.app_role() in (''admin'',''device''))', t);
  end loop;
end $$;
