from pathlib import Path
import subprocess,socket,time,threading,queue,json,shutil,tempfile,os
repo=Path(__file__).resolve().parents[3]
scratch=Path(tempfile.mkdtemp(prefix='cnb-mqtt-test-'))
print(f'Temporary test files: {scratch}',flush=True)
for name in ('common.ps1','run-car.ps1','stop-car.ps1','set-config.ps1'):
 shutil.copyfile(repo/'tools/mqtt'/name,scratch/name)
with socket.socket() as reserve:
 reserve.bind(('127.0.0.1',0)); port=reserve.getsockname()[1]
(scratch/'broker.conf').write_text(f'listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\n')
(scratch/'.env').write_text(f'CNB_MQTT_HOST=127.0.0.1\nCNB_MQTT_PORT={port}\nCNB_MQTT_USERNAME=local-test\nCNB_MQTT_PASSWORD=test quote" and slash\\\n')
mosq=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Mosquitto'; flags=subprocess.CREATE_NO_WINDOW
broker=subprocess.Popen([str(mosq/'mosquitto.exe'),'-c',str(scratch/'broker.conf')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=flags)
children=[]
def publish(state):
 subprocess.run([str(mosq/'mosquitto_pub.exe'),'-h','127.0.0.1','-p',str(port),'-t','cnb/vagrant/command/state','-q','1','-s'],input=json.dumps(state),text=True,check=True,timeout=3,creationflags=flags)
try:
 time.sleep(0.4)
 sub=subprocess.Popen([str(mosq/'mosquitto_sub.exe'),'-h','127.0.0.1','-p',str(port),'-t','cnb/vagrant/command','-q','1'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,creationflags=flags)
 children.append(sub); received=queue.Queue()
 def reader():
  for line in sub.stdout:
   try: received.put(json.loads(line))
   except ValueError: received.put({'command':'INVALID_JSON','raw':line})
 threading.Thread(target=reader,daemon=True).start(); time.sleep(0.2)
 for scenario in ('accepted','no_ack','rejected'):
  while not received.empty(): received.get()
  run=subprocess.Popen(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(scratch/'run-car.ps1')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,creationflags=flags)
  children.append(run); commands=[]; deadline=time.monotonic()+12; session=None
  while time.monotonic()<deadline:
   try: msg=received.get(timeout=0.1)
   except queue.Empty:
    if run.poll() is not None: break
    continue
   commands.append(msg['command'])
   if msg['command']=='start':
    session=msg['session_id']; request=msg['request_id']
    if scenario!='no_ack':
     publish({'schema_version':1,'last_request_id':request,'session_id':session,
      'result':scenario,'control_state':'armed' if scenario=='accepted' else 'disarmed','reason':'none'})
   elif msg['command']=='heartbeat':
    assert scenario=='accepted' and msg['session_id']==session
    publish({'schema_version':1,'last_request_id':request,'session_id':'','result':'state','control_state':'disarmed','reason':'operator_stop'})
   elif msg['command']=='stop': break
  try: output=run.communicate(timeout=5)[0]
  except subprocess.TimeoutExpired:
   run.kill(); output=run.communicate()[0]; raise AssertionError(f'{scenario} hung: {output}')
  expected=['start','heartbeat','stop'] if scenario=='accepted' else (['start','start','stop'] if scenario=='no_ack' else ['start','stop'])
  assert commands==expected,(scenario,commands,output)
  assert ('Car accepted session' in output)==(scenario=='accepted'),output
  print(f'PASS {scenario}: '+', '.join(commands),flush=True)
 # Verify the operator's mode names, retained wire JSON and legacy omission.
 config_sub=subprocess.Popen([str(mosq/'mosquitto_sub.exe'),'-h','127.0.0.1','-p',str(port),'-t','cnb/vagrant/config/set','-q','1'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,creationflags=flags)
 children.append(config_sub); configurations=queue.Queue()
 def config_reader():
  for line in config_sub.stdout: configurations.put(json.loads(line))
 threading.Thread(target=config_reader,daemon=True).start(); time.sleep(0.2)
 config_command=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(scratch/'set-config.ps1'),'-StopDistanceCm','35','-DriveDuty','0','-TelemetryIntervalMs','200']
 last_revision=0
 for name,wire in [('SlowLeft','slow_left'),('SlowRight','slow_right'),('GradualSweep','gradual_sweep'),('DecideAction','decide_action'),(None,None)]:
  args=config_command+(['-DriverStyle',name] if name else [])
  result=subprocess.run(args,text=True,capture_output=True,creationflags=flags,timeout=8)
  assert result.returncode==0,(result.stdout,result.stderr)
  payload=configurations.get(timeout=3)
  assert payload.get('driver_style')==wire,payload
  assert ('driver_style' in payload)==(name is not None),payload
  assert payload['drive_duty']==0 and payload['stop_distance_cm']==35 and payload['telemetry_interval_ms']==200,payload
  assert payload['revision']>last_revision,payload
  last_revision=payload['revision']
  retained=subprocess.run([str(mosq/'mosquitto_sub.exe'),'-h','127.0.0.1','-p',str(port),'-t','cnb/vagrant/config/set','-C','1','-W','2'],text=True,capture_output=True,creationflags=flags,timeout=3)
  assert retained.returncode==0 and json.loads(retained.stdout)==payload,retained.stderr
 invalid=subprocess.run(config_command+['-DriverStyle','unknown'],text=True,capture_output=True,creationflags=flags,timeout=5)
 assert invalid.returncode!=0 and configurations.empty(),invalid.stdout
 print('PASS driver style mapping, retained configuration, optional omission and invalid style',flush=True)
 # Future stored revision must not go backwards when wall clock is behind it.
 (scratch/'.revision').write_text('4000000000')
 result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-Command',f". '{scratch/'common.ps1'}'; Get-CnbNextRevision"],text=True,capture_output=True,creationflags=flags,timeout=5)
 assert result.returncode==0 and result.stdout.strip()=='4000000001',(result.stdout,result.stderr)
 print('PASS future revision counter',flush=True)
finally:
 for child in children+[broker]:
  if child.poll() is None: child.kill()
  child.wait()
