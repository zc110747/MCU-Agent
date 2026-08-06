set confirm off
set pagination off
set print pretty off

target extended-remote localhost:3333
monitor reset halt

# Run the whole boot sequence, stop when entering the main loop.
tbreak main.c:74
continue

printf "\n[S2] ================= storage =================\n"
printf "[S2] sdcard mounted flag  = %d\n", 'bsp_sdcard.c'::s_mounted
printf "[S2] FATFS fs_type        = %d (3=FAT32, 4=exFAT)\n", 'bsp_sdcard.c'::s_fatfs.fs_type
printf "[S2] FATFS csize (sect/cl)= %u\n", 'bsp_sdcard.c'::s_fatfs.csize
printf "[S2] FATFS n_fatent       = %u\n", 'bsp_sdcard.c'::s_fatfs.n_fatent

printf "\n[S2] ================= slideshow ===============\n"
printf "[S2] s_ready              = %d\n", 'app_slideshow.c'::s_ready
printf "[S2] s_file_count         = %u\n", 'app_slideshow.c'::s_file_count
printf "[S2] s_index              = %u\n", 'app_slideshow.c'::s_index

set $i = 0
while $i < 'app_slideshow.c'::s_file_count
  printf "[S2]   file[%2d] = %s\n", $i, 'app_slideshow.c'::s_files[$i]
  set $i = $i + 1
end

printf "\n[S2] ================= framebuffer =============\n"
printf "[S2] first 8 px of frame  = "
set $j = 0
while $j < 8
  printf "%04x ", app_image_framebuffer()[$j]
  set $j = $j + 1
end
printf "\n"

quit
