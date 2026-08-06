set confirm off
set pagination off
set print pretty off

target extended-remote localhost:3333
monitor reset halt

# Stop right after the frame buffer has been filled, before it is pushed
# out over SPI.  app_slideshow.c:148 == bsp_oled_blit_frame(...)
break app_slideshow.c:148

continue
printf "\n[S3] ---- capture 0 ----\n"
printf "[S3] file        = %s\n", 'app_slideshow.c'::s_files['app_slideshow.c'::s_index]
printf "[S3] src         = %u x %u\n", info.src_width, info.src_height
printf "[S3] hw descale  = 1/%u\n", 1 << info.scale
printf "[S3] crop side   = %u\n", info.crop_side
printf "[S3] decode time = %u ms\n", info.elapsed_ms
dump binary memory tools/cap/frame0.bin &'app_image.c'::s_framebuffer[0] &'app_image.c'::s_framebuffer[57600]

continue
printf "\n[S3] ---- capture 1 ----\n"
printf "[S3] file        = %s\n", 'app_slideshow.c'::s_files['app_slideshow.c'::s_index]
printf "[S3] src         = %u x %u\n", info.src_width, info.src_height
printf "[S3] hw descale  = 1/%u\n", 1 << info.scale
printf "[S3] crop side   = %u\n", info.crop_side
printf "[S3] decode time = %u ms\n", info.elapsed_ms
dump binary memory tools/cap/frame1.bin &'app_image.c'::s_framebuffer[0] &'app_image.c'::s_framebuffer[57600]

continue
printf "\n[S3] ---- capture 2 ----\n"
printf "[S3] file        = %s\n", 'app_slideshow.c'::s_files['app_slideshow.c'::s_index]
printf "[S3] src         = %u x %u\n", info.src_width, info.src_height
printf "[S3] hw descale  = 1/%u\n", 1 << info.scale
printf "[S3] crop side   = %u\n", info.crop_side
printf "[S3] decode time = %u ms\n", info.elapsed_ms
dump binary memory tools/cap/frame2.bin &'app_image.c'::s_framebuffer[0] &'app_image.c'::s_framebuffer[57600]

printf "\n[S3] uwTick at end = %u\n", uwTick
quit
