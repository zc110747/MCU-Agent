set pagination off
set confirm off
target remote :3333
monitor reset halt
continue &
shell sleep 3
interrupt
printf "frames=%u fps=%u overruns=%u last_idx=%u ready=%d\n", g_dcmi_frames, g_dcmi_fps, g_dcmi_overruns, g_dcmi_last_idx, s_fd_ready
echo --- backtrace ---\n
bt
detach
