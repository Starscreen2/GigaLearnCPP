import site
import sys
import json
import os
import time

wandb_run = None
wandb = None
init_params = None  # Store init parameters for reconnection
last_reconnect_attempt = 0
reconnect_cooldown = 5  # Seconds between reconnection attempts
error_count = 0  # Track connection errors to limit logging
non_conn_error_count = 0  # Track non-connection errors to limit logging

# Takes in the python executable path, the three wandb init strings, and optionally the current run ID
# Returns the ID of the run (either newly created or resumed)
def init(py_exec_path, project, group, name, id = None):

	global wandb_run, wandb, init_params
	
	# Fix the path of our interpreter so wandb doesn't run RLGym_PPO instead of Python
	# Very strange fix for a very strange problem
	sys.executable = py_exec_path
	
	try:
		site_packages_dir = os.path.join(os.path.join(os.path.dirname(py_exec_path), "Lib"), "site-packages")
		sys.path.append(site_packages_dir)
		site.addsitedir(site_packages_dir)
		import wandb
	except Exception as e:
		raise Exception(f"""
			FAILED to import wandb! Make sure GigaLearnCPP isn't using the wrong Python installation.
			This installation's site packages: {site.getsitepackages()}
			Exception: {repr(e)}"""
		)
	
	# Store init parameters for reconnection
	init_params = {
		'py_exec_path': py_exec_path,
		'project': project,
		'group': group,
		'name': name,
		'id': id
	}
	
	# Enable online mode with settings to prevent blocking
	# Disable console output to reduce I/O overhead
	os.environ['WANDB_CONSOLE'] = 'off'
	
	print("Calling wandb.init() in ONLINE mode...")
	try:
		if not (id is None) and len(id) > 0:
			wandb_run = wandb.init(project = project, group = group, name = name, id = id, resume = "allow", settings = wandb.Settings(_disable_stats=True))
		else:
			wandb_run = wandb.init(project = project, group = group, name = name, settings = wandb.Settings(_disable_stats=True))
		return wandb_run.id
	except Exception as e:
		# If init fails, fall back to offline mode to prevent blocking
		print(f"WARNING: WandB init failed, falling back to offline mode: {repr(e)}")
		os.environ['WANDB_MODE'] = 'offline'
		if not (id is None) and len(id) > 0:
			wandb_run = wandb.init(project = project, group = group, name = name, id = id, resume = "allow", mode = "offline")
		else:
			wandb_run = wandb.init(project = project, group = group, name = name, mode = "offline")
		return wandb_run.id

def reconnect():
	"""Attempt to reconnect wandb using stored init parameters - non-blocking with timeout"""
	global wandb_run, init_params, last_reconnect_attempt
	
	if init_params is None:
		print("WARNING: Cannot reconnect wandb - no init parameters stored")
		return False
	
	current_time = time.time()
	if current_time - last_reconnect_attempt < reconnect_cooldown:
		# Still in cooldown, don't attempt reconnection yet
		return False
	
	last_reconnect_attempt = current_time
	
	try:
		print("Attempting to reconnect wandb...")
		# Close existing run if it exists (non-blocking)
		if wandb_run is not None:
			try:
				wandb_run.finish()
			except:
				pass  # Ignore errors when finishing dead run
		
		# Disable console output to reduce I/O overhead
		os.environ['WANDB_CONSOLE'] = 'off'
		
		# Try online mode first, but with timeout protection
		params = init_params
		try:
			if not (params['id'] is None) and len(params['id']) > 0:
				wandb_run = wandb.init(
					project = params['project'],
					group = params['group'],
					name = params['name'],
					id = params['id'],
					resume = "allow",
					settings = wandb.Settings(_disable_stats=True)
				)
			else:
				wandb_run = wandb.init(
					project = params['project'],
					group = params['group'],
					name = params['name'],
					settings = wandb.Settings(_disable_stats=True)
				)
			print(f"Successfully reconnected wandb (online). Run ID: {wandb_run.id}")
			return True
		except Exception as online_error:
			# If online reconnect fails, fall back to offline mode to prevent blocking
			print(f"WARNING: Online reconnect failed, using offline mode: {repr(online_error)}")
			os.environ['WANDB_MODE'] = 'offline'
			if not (params['id'] is None) and len(params['id']) > 0:
				wandb_run = wandb.init(
					project = params['project'],
					group = params['group'],
					name = params['name'],
					id = params['id'],
					resume = "allow",
					mode = "offline"
				)
			else:
				wandb_run = wandb.init(
					project = params['project'],
					group = params['group'],
					name = params['name'],
					mode = "offline"
				)
			print(f"Successfully reconnected wandb (offline). Run ID: {wandb_run.id}")
			return True
	except Exception as e:
		print(f"WARNING: Failed to reconnect wandb: {repr(e)}")
		return False

def add_metrics(metrics):
	global wandb_run
	
	if wandb_run is None:
		# Try to reconnect if we don't have a run (non-blocking)
		if not reconnect():
			return  # Can't send metrics without a connection - silently skip to prevent blocking
	
	try:
		# wandb.log() is already async by default - it queues metrics and sends in background
		# This should not block training, but we catch errors just in case
		wandb_run.log(metrics)
	except (ConnectionError, OSError, Exception) as e:
		# Connection error - attempt to reconnect ONCE (non-blocking with cooldown)
		error_str = str(e).lower()
		is_connection_error = (
			'socket' in error_str or
			'connection' in error_str or
			'network' in error_str or
			'reset' in error_str or
			'timeout' in error_str
		)
		
		if is_connection_error:
			# Only log first few connection errors to avoid spam
			global error_count
			error_count += 1
			if error_count <= 3:
				print(f"WARNING: Wandb connection error detected: {repr(e)}")
				print("Attempting to reconnect (non-blocking)...")
			
			# Reconnect (has cooldown built-in to prevent spam)
			if reconnect():
				# Retry sending metrics after successful reconnection (one retry only)
				try:
					wandb_run.log(metrics)
					if error_count <= 3:
						print("Successfully sent metrics after reconnection")
				except Exception as retry_error:
					if error_count <= 3:
						print(f"WARNING: Failed to send metrics after reconnection: {repr(retry_error)}")
			else:
				if error_count <= 3:
					print("WARNING: Reconnection failed. Metrics not sent this iteration.")
		else:
			# Non-connection error - only log first few to avoid spam
			global non_conn_error_count
			non_conn_error_count += 1
			if non_conn_error_count <= 3:
				print(f"WARNING: Wandb error (non-connection): {repr(e)}")