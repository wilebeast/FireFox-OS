#!/bin/bash

SCRIPT_NAME=$(basename $0)

ADB=adb
PROFILE_DIR=/data/local/tmp
PROFILE_PATTERN=${PROFILE_DIR}/'profile_?_*.txt';
PREFIX=""

# The get_pids function populates B2G_PIDS as an array containting the PIDs
# of all of the b2g processes
declare -a B2G_PIDS

# The get_comms function populates B2G_COMMS as an array mapping
# pids to comms (process names). get_comms also causes B2G_PIDS to be populated.
declare -a B2G_COMMS

###########################################################################
#
# Clears the B2G_PIDS, which will force the information to be refetched.
#
clear_pids() {
  unset B2G_PIDS
  unset B2G_COMMS
}

###########################################################################
#
# Takes a pid or process name, and tries to determine the PID (only accepts
# PIDs which are associated with a b2g process)
#
find_pid() {
  local search="$1"
  get_comms
  for pid in ${B2G_PIDS[*]}; do
    if [ "${search}" == "${pid}" -o "${search}" == "${B2G_COMMS[${pid}]}" ]; then
      echo -n "${pid}"
      return
    fi
  done
}

###########################################################################
#
# Sets B2G_PID to be the pid of the b2g process.
#
get_b2g_pid() {
   echo $($ADB shell 'toolbox ps b2g | (read header; read user pid rest; echo -n $pid)')
}

###########################################################################
#
# Fill B2G_PIDS with an array of b2g process PIDs
#
get_pids() {
  if [ ${#B2G_PIDS[*]} -gt 0 ]; then
    # We've already populated B2G_PIDS, don't bother doing it again
    return
  fi

  B2G_PIDS=($(${ADB} shell ps | while read line; do
    if [ "${line/*b2g*/b2g}" = "b2g" ]; then
      echo ${line} | (
        read user pid rest;
        echo -n "${pid} "
      )
    fi
  done))
}

###########################################################################
#
# Fill B2G_COMMS such that B2G_COMMS[pid] contains the process name.
#
get_comms() {
  if [ ${#B2G_COMMS[*]} -gt 0 ]; then
    # We've already populated B2G_COMMS, don't bother doing it again
    return
  fi
  get_pids
  for pid in ${B2G_PIDS[*]}; do
    # adb shell seems to replace the \n with a \r, so we use
    # tr to strip trailing newlines or CRs
    B2G_COMMS[${pid}]="$(${ADB} shell cat /proc/${pid}/comm | tr -d '\r\n')"
  done
}

###########################################################################
#
# Determines if the profiler is running for a given pid
#
# We use the traditional 0=success 1=failure return code.
#
is_profiler_running() {
  local pid=$1
  if [ -z "${pid}" ]; then
    return 1
  fi
  local status="$(${ADB} shell cat /proc/${pid}/environ | tr '\0' '\n' | grep 'MOZ_PROFILER_STARTUP=1')"
  if [ -z "${status}" ]; then
    return 1
  fi
  return 0
}

###########################################################################
#
# Removes any stale profile files which might be left on the device
#
remove_profile_files() {
  echo -n "Removing old profile files (from device) ..."
  for file in $(${ADB} shell echo -n ${PROFILE_PATTERN}); do
    # If no files match the pattern, then echo will return the pattern
    if [ "${file}" != "${PROFILE_PATTERN}" ]; then
      echo -n "."
      ${ADB} shell rm ${file}
    fi
  done
  echo " done"
}

###########################################################################
#
# Capture the profiling information from a given process.
#
HELP_capture="Signals, pulls, and symbolicates the profile data"
cmd_capture() {
  # Send the signal right away. If the profiler wasn't started we'll catch
  # that later.
  cmd_signal "$1"
  # Verify that b2g was started with the profiler enabled
  if ! is_profiler_running $(get_b2g_pid); then
    echo "Profiler doesn't seem to be running"
    echo "Did you start the profiler using ${SCRIPT_NAME} start ?"
    exit 1
  fi
  get_comms
  declare -a local_filename
  local timestamp=$(date +"%H%M")
  local stabilized
  if [ "${CMD_SIGNAL_PID:0:1}" == "-" ]; then
    # We signalled the entire process group. Stabilize, pull and symbolicate
    # each file in parallel
    for pid in ${B2G_PIDS[*]}; do (
      PREFIX="     ${pid}"
      PREFIX="${PREFIX:$((${#PREFIX} - 5)):5}: "
      echo "${PREFIX}Stabilizing ${B2G_COMMS[${pid}]} ..." 1>&2
      stabilized=$(cmd_stabilize ${pid})
      if [ "${stabilized}" == "0" ]; then
        echo "${PREFIX}Process was probably killed due to OOM" 1>&2
      else
        cmd_pull ${pid} "${B2G_COMMS[${pid}]}" ${timestamp}
        if [ ! -z "${CMD_PULL_LOCAL_FILENAME}" -a -s "${CMD_PULL_LOCAL_FILENAME}" ]; then
          cmd_symbolicate "${CMD_PULL_LOCAL_FILENAME}"
        else
          echo "${PREFIX}PULL FAILED for ${pid}" 1>&2
        fi
      fi) &
    done
    # This sleep just delays the "Waiting for stuff to finish" echo slightly
    # so that it shows up after the Stabilizing echos from above. The
    # stabilizing loop will delay for at least two seconds, so this has no
    # impact on the performance, it just makes the output look a big nicer.
    sleep 1
    echo "Waiting for stabilize/pull/symbolicate to finish ..."
    wait
    echo "Done"
  else
    pid="${CMD_SIGNAL_PID}"
    echo "Stabilizing ${pid} ${B2G_COMMS[${pid}]} ..." 1>&2
    stabilized=$(cmd_stabilize ${pid})
    if [ "${stabilized}" == "0" ]; then
      echo "Process ${pid} was probably killed due to OOM" 1>&2
    else
      cmd_pull ${pid} "${B2G_COMMS[${pid}]}"
      if [ ! -z "${CMD_PULL_LOCAL_FILENAME}" -a -s "${CMD_PULL_LOCAL_FILENAME}" ]; then
        cmd_symbolicate "${CMD_PULL_LOCAL_FILENAME}"
      fi
    fi
  fi
  # cmd_pull should remove each file as we pull it. This just covers the
  # case where it doesn't
  remove_profile_files
}

###########################################################################
#
# Display a brief help message for each supported command
#
HELP_help="Shows these help messages"
cmd_help() {
  if [ "$1" == "" ]; then
    echo "Usage: ${SCRIPT_NAME} command [args]"
    echo "where command is one of:"
    for command in ${allowed_commands}; do
      desc=HELP_${command}
      printf "  %-11s %s\n" ${command} "${!desc}"
    done
  else
    command=$1
    if [ "${allowed_commands/*${command}*/${command}}" == "${command}" ]; then
      desc=HELP_${command}
      printf "%-11s %s\n" ${command} "${!desc}"
    else
      echo "Unrecognized command: '${command}'"
    fi
  fi
}

###########################################################################
#
# Show all of the profile files on the phone
#
HELP_ls="Shows the profile files on the phone"
cmd_ls() {
  ${ADB} shell "cd ${PROFILE_DIR}; ls -l profile_?_*.txt"
}

###########################################################################
#
# Show all of the b2g processes which are currently running.
#
HELP_ps="Shows the B2G processes"
cmd_ps() {
  local status
  get_comms
  echo "  PID Name"
  echo "----- ----------------"
  for pid in ${B2G_PIDS[*]}; do
    if is_profiler_running ${pid}; then
      status="profiler running"
    else
      status="profiler not running"
    fi
    printf "%5d %-16s %s\n" "${pid}" "${B2G_COMMS[${pid}]}" "${status}"
  done
}

###########################################################################
#
# Pulls the profile file from the phone
#
HELP_pull="Pulls the profile data from the phone"
cmd_pull() {
  local pid=$1
  local comm=$2
  local label=$3

  # The profile data gets written to /data/local/tmp/profile_X_PID.txt
  # where X is the XRE_ProcessType (so 0 for the b2g process, 2 for
  # the plugin containers).
  #
  # We know the PID, so we look for the file, and we wait for it to
  # stabilize.

  local attempt
  local profile_filename
  local profile_pattern="${PROFILE_DIR}/profile_?_${pid}.txt"
  local local_filename
  if [ -z "${comm}" ]; then
    local_filename="profile_${pid}.txt"
  elif [ -z "${label}" ]; then
    local_filename="profile_${pid}_${B2G_COMMS[${pid}]}.txt"
  else
    local_filename="profile_${label}_${pid}_${B2G_COMMS[${pid}]}.txt"
  fi
  profile_filename=$(${ADB} shell "echo -n ${profile_pattern}")
  
  CMD_PULL_LOCAL_FILENAME=
  if [ "${profile_filename}" == "${profile_pattern}" ]; then
    echo "${PREFIX}Profile file for PID ${pid} ${B2G_COMMS[${pid}]} doesn't exist - process likely killed due to OOM"
    return
  fi
  echo "${PREFIX}Pulling ${profile_filename} into ${local_filename}"
  ${ADB} pull ${profile_filename} "${local_filename}" > /dev/null 2>&1
  #echo "${PREFIX}Removing ${profile_filename}"
  ${ADB} shell rm ${profile_filename}

  if [ ! -s "${local_filename}" ]; then
    echo "${PREFIX}Profile file for PID ${pid} ${B2G_COMMS[${pid}]} is empty - process likely killed due to OOM"
    return
  fi
  CMD_PULL_LOCAL_FILENAME="${local_filename}"
}

###########################################################################
#
# Signal the profiler to generate the profile data
#
HELP_signal="Signal the profiler to generate profile data"
cmd_signal() {
  # Call get_comms here since we need it later. find_pid is launched in
  # a sub-shell since we want the echo'd output which means we won't
  # see the results when it calls get_comms, but if we call get_comms,
  # then find_pid will see the results of us calling get_comms.
  get_comms
  local pid
  if [ -z "$1" ]; then
    # If no pid is specified, then send a signal to the b2g process group.
    # This will cause the signal to go to b2g and all of it subprocesses.
    pid=-$(find_pid b2g)
    echo "Signalling Process Group: ${pid:1} ${B2G_COMMS[${pid:1}]} ..."
  else
    pid=$(find_pid "$1")
    if [ "${pid}" == "" ]; then
      echo "Must specify a PID or process-name to capture"
      cmd_ps
      exit 1
    fi
    echo "Signalling PID: ${pid} ${B2G_COMMS[${pid}]} ..."
  fi

  ${ADB} shell "kill -12 ${pid}"
  CMD_SIGNAL_PID=${pid}
}

###########################################################################
#
# Wait for a single captured profile files to stabilize.
#
# This is somewhat complicated by the fact that writing the files on the
# phone is sort of serialized. I observe that 2 files actually get changing
# data, and then when those are finished, 2 more will get data.
#
# So when trying to figure out how long to wait for a file to get a
# non-zero size, we need to wait until no files are changing before starting
# our timeout.
#

HELP_stabilzie="Waits for a profile file to stop changing"
cmd_stabilize() {

  local pid=$1
  if [ -z "$1" ]; then
    echo "No PID specified." 1>&2
    return
  fi

  # We wait for the output of ls ${PROFILE_PATTERN} to stay the same for
  # a few seconds in a row

  local attempt
  local prev_size
  local prev_sizes
  local curr_size
  local curr_sizes
  local stabilized=0
  local waiting=0

  while true; do
    curr_sizes=$(${ADB} shell ls -l ${PROFILE_DIR}/'profile_?_*.txt' |
      while read line; do
        echo ${line} | (
          read perms user group size rest;
          echo -n "${size} "
        )
      done)
    curr_size=$(${ADB} shell ls -l ${PROFILE_DIR}/'profile_?_'${pid}'.txt' | (read perms user group size rest; echo -n ${size}))
    if [ "${curr_size}" == "0" ]; then
      # Our file hasn't changed. See if others have
      if [ "${curr_sizes}" == "${prev_sizes}" ]; then
        waiting=$(( ${waiting} + 1 ))
        if [ ${waiting} -gt 5 ]; then
          # No file sizes have changed in the last 5 seconds.
          # Assume that our PID was OOM'd
          break
        fi
      else
        waiting=0
      fi
    else
      # Our file has non-zero size
      if [ "${curr_size}" == "${prev_size}" ]; then
        waiting=$(( ${waiting} + 1 ))
        if [ ${waiting} -gt 2 ]; then
          # Our size is non-zero and hasn't changed recently.
          # Consider it to be stabilized
          stabilized=1
          break
        fi
      else
        waiting=0
      fi
    fi
    prev_size="${curr_size}"
    prev_sizes="${curr_sizes}"
    sleep 1
  done

  echo "${stabilized}"
}

###########################################################################
#
# Start b2g with the profiler enabled.
#
HELP_start="Starts the profiler"
cmd_start() {
  stop_b2g
  remove_profile_files
  echo -n "Starting b2g with profiling enabled ..."
  # Use nohup or we may accidentally kill the adb shell when this
  # script exits.
  nohup ${ADB} shell "MOZ_PROFILER_STARTUP=1 /system/bin/b2g.sh > /dev/null" > /dev/null 2>&1 &
  echo " started"
}

###########################################################################
#
# Stop profiling and start b2g normally
#
HELP_stop="Stops profiling and restarts b2g normally."
cmd_stop() {
  stop_b2g
  echo "Restarting b2g (normally) ..."
  ${ADB} shell start b2g
}

###########################################################################
#
# Add symbols to a captured profile using the libraries from our build
# tree.
#
HELP_symbolicate="Add symbols to a captured profile"
cmd_symbolicate() {
  local profile_filename="$1"
  if [ -z "${profile_filename}" ]; then
    echo "${PREFIX}Expecting the filename containing the profile data"
    exit 1
  fi
  if [ ! -f "${profile_filename}" ]; then
    echo "${PREFIX}File ${profile_filename} doesn't exist"
    exit 1
  fi

  # Get some variables from the build system
  local var_profile="./.var.profile"
  if [ ! -f ${var_profile} ]; then
    echo "${PREFIX}Unable to locate ${var_profile}"
    echo "${PREFIX}You need to build b2g in order to get symbolic information"
    exit 1
  fi
  source ${var_profile}

  local sym_filename="${profile_filename%.*}.sym"
  echo "${PREFIX}Adding symbols to ${profile_filename} and creating ${sym_filename} ..."
  ./scripts/profile-symbolicate.py -o "${sym_filename}" "${profile_filename}" > /dev/null
}

###########################################################################
#
# Tries to stop b2g
#
stop_b2g() {
  local pid
  local attempt
  if is_profiler_running $(get_b2g_pid); then
    echo -n "Profiler appears to be running."
  else
    # Note: stop sends SIGKILL, but only if b2g was launched via start
    # If b2g was launched via the debugger or the profiler, then stop is
    # essentially a no-op
    echo -n "Stopping b2g ..."
    ${ADB} shell "stop b2g"
    for attempt in 1 2 3 4 5; do
      pid=$(${ADB} shell 'toolbox ps b2g | (read header; read user pid rest; echo -n ${pid})')
      if [ "${pid}" == "" ]; then
        break
      fi
      echo -n "."
      sleep 1
    done
  fi

  # Now do a cleanup check and make sure that all of the b2g pids are actually gone
  clear_pids
  get_pids
  if [ "${#B2G_PIDS[*]}" -gt 0 ]; then
    echo
    echo -n "Killing b2g ..."
    for pid in ${B2G_PIDS[*]}; do
      ${ADB} shell "kill -9 ${pid}"
    done
    for attempt in 1 2 3 4 5; do
      clear_pids
      get_pids
      if [ "${#B2G_PIDS[*]}" == 0 ]; then
        break
      fi
      echo -n "."
      sleep 1
    done
  fi

  # And if things still haven't shutdown, then give up
  clear_pids
  get_pids
  if [ "${#B2G_PIDS[*]}" -gt 0 ]; then
    echo
    echo "b2g doesn't seem to want to go away. Try rebooting."
    exit 1
  fi
  echo " stopped."
}

###########################################################################
#
# Determine if the first argument is a valid command and execute the
# corresponding function if it is.
#
allowed_commands=$(declare -F | sed -ne 's/declare -f cmd_\(.*\)/\1/p' | tr "\n" " ")
command=$1
if [ "${command}" == "" ]; then
  cmd_help
  exit 0
fi
if [ "${allowed_commands/*${command}*/${command}}" == "${command}" ]; then
  shift
  cmd_${command} "$@"
else
  echo "Unrecognized command: '${command}'"
fi

