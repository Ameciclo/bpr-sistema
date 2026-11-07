#!/usr/bin/env python3
"""Compile original firmware against controlled host stubs and verify audit reproductions."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parent.parent
COMMON=['g++','-std=c++17','-O0','-g','-I','audit-tests/stubs','-I','audit-deps/ArduinoJson/src']
CROOT=Path('bpr-sistema/firmware/central/src')
CLIB=['config_manager.cpp','config_credentials.cpp','buffer_manager.cpp','bpr_json_helper.cpp','endpoints.cpp','time_sync.cpp']
checks={
'bici_checks':(COMMON+['-I','bpr-sistema/firmware/bici/include','audit-tests/bici_checks.cpp']+[str(p) for p in (ROOT/'bpr-sistema/firmware/bici/src').glob('*.cpp')],{
'boot_after_6_loops':'0','wifi_scans_after_6_loops':'0','saved_interval_after_boot_sequence':'300','one_network_payload_bytes':'134','saved_after_endSession':'no','loaded_before_startSession':'data','loaded_after_startSession':'empty','addScan_after_clear':'empty','dev_mode_after_false_update':'true','scan_records_for_3_networks':'3','networks_in_first_record':'1','buffer_after_failed_write':'empty','id_for_10000000':'bpr-1000000','id_for_10000001':'bpr-1000000','session_struct_bytes':'54324'}),
'central_checks':(COMMON+['-I','bpr-sistema/firmware/central/include','-I','bpr-sistema/firmware/common','audit-tests/central_checks.cpp']+[str(CROOT/p) for p in CLIB],{
'allow_bike_from_downloaded_registry':'no','runtime_status_count':'0','config_slots_after_11_updates_same_bike':'10','hasConfigUpdate_after_download':'false','zero_led_config_accepted':'yes','central_config_last_update':'10','buffer_files_after_two_adds':'2','buffer_files_after_upload_preparation':'4','addData_when_flash_write_fails':'success','network_packet_134_bytes_accepted':'no','bike_config_downloads_first_sync':'0','hex_id_central_roundtrip':'bpr-000000'}),
'ble_checks':(COMMON+['-I','bpr-sistema/firmware/central/include','-I','bpr-sistema/firmware/common','audit-tests/ble_checks.cpp'],{
'handle_zero_identified':'no','handle_zero_timeout_removed':'no','first_empty_handle_assigned_without_sender':'bpr-123456','arrival_callbacks_for_two_writes_same_bike':'2','notification_calls':'1'}),
'retry_checks':(COMMON+['-Df_str=c_str','-I','bpr-sistema/firmware/central/include','-I','bpr-sistema/firmware/common','audit-tests/retry_checks.cpp']+[str(p) for p in (ROOT/CROOT).glob('*.cpp') if p.name!='config_ap.cpp'],{
'failure_loop_1':'state=CLOUD_SYNC, elapsed_ms=1500','failure_loop_2':'state=CLOUD_SYNC, elapsed_ms=0','failure_loop_3':'state=CLOUD_SYNC, elapsed_ms=0','failure_loop_4':'state=CLOUD_SYNC, elapsed_ms=0','failure_loop_5':'state=TEMP_CONFIG_AP, elapsed_ms=0'})}
checks['portal_checks']=(COMMON+['-I','bpr-sistema/firmware/central/include','-I','bpr-sistema/firmware/common','audit-tests/portal_checks.cpp']+[str(CROOT/p) for p in ['config_ap.cpp','config_manager.cpp','config_credentials.cpp']],{
'flat_json_response':'200','flat_json_api_key_saved':'no','flat_json_credentials_valid':'no'})
count=0
for name,(cmd,expected) in checks.items():
 target='audit-tests/'+name
 built=subprocess.run(cmd+['-o',target],cwd=ROOT,capture_output=True,text=True)
 (ROOT/('audit-tests/'+name+'-build.log')).write_text(built.stdout+built.stderr)
 if built.returncode:print(built.stderr);sys.exit(built.returncode)
 out=subprocess.check_output(['./'+target],cwd=ROOT,text=True)
 (ROOT/('audit-tests/'+name+'-results.txt')).write_text(out)
 values=dict(line.split(': ',1) for line in out.splitlines())
 for key,value in expected.items():
  assert values.get(key)==value,(name,key,values.get(key),value)
  count+=1
 print(name+': '+str(len(expected))+' observed behaviors reproduced')
cmd=checks['central_checks'][0]
subprocess.run(cmd+['-fsanitize=address','-fno-omit-frame-pointer','-o','audit-tests/central_checks_asan'],cwd=ROOT,check=True)
import os
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
r=subprocess.run(['./audit-tests/central_checks_asan','malformed'],cwd=ROOT,env=env,capture_output=True,text=True)
(ROOT/'audit-tests/asan-results.txt').write_text(r.stdout+r.stderr)
assert r.returncode!=0 and 'buffer_manager.cpp' in r.stderr, 'Expected malformed buffer memory fault was not reproduced'
print('malformed buffer: memory fault reproduced under ASan')
print(str(count)+' output checks plus malformed-buffer memory fault verified. These confirm defects; they do not mean the firmware is correct.')
