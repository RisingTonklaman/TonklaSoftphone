# SIP-record-video (Windows, PJSIP + OpenH264)

โปรเจกต์ตัวอย่าง Softphone แบบ C++/CMake ใช้ PJSIP (pjsua2 + pjsua C API) เปิดวิดีโอ H.264 และเปลี่ยนชื่อหน้าต่าง SDL2 เพื่อให้อัดหน้าจอด้วย ffmpeg ได้ง่าย มีคำสั่งโทร/รับ/แขวน และพรีวิวกล้อง

จุดประสงค์ของ README นี้คือให้เครื่องอื่นๆ clone แล้วใช้งาน/คอมไพล์ได้ง่าย โดยมีสคริปต์เตรียมแวดล้อมครบสำหรับ Windows x64

## คุณสมบัติ
- PJSIP pjsua2 + วิดีโอ (SDL2 renderer, DirectShow capture)
- บังคับใช้ H.264 เป็นลำดับแรกของโค้ดแคค
- พรีวิวกล้อง/แสดงวิดีโอปลายทาง พร้อมตั้งชื่อหน้าต่างสำหรับ ffmpeg
- สคริปต์เตรียมแวดล้อม: vcpkg + SDL2, OpenH264, PJSIP (จากโฟลเดอร์ในรีโป)

## ความต้องการเบื้องต้น (Windows 10/11 x64)
- Visual Studio 2022 หรือ Build Tools (Desktop development with C++)
- Git, CMake (สคริปต์จะตรวจและใช้งาน)
- อินเทอร์เน็ตสำหรับดาวน์โหลด dependencies (vcpkg/SDL2, OpenH264)

## โครงสร้าง Dependencies
- PJSIP: เวนเดอร์ไฟล์ติดตั้งไว้ที่ `vendor/pjsip/install` (headers+lib พร้อมใช้งาน)
- OpenH264: โคลนและบิลด์ไว้ที่ `third_party/openh264` ผลลัพธ์อยู่ใน `third_party/openh264/build_x64`
- SDL2: ติดตั้งผ่าน vcpkg ที่ `third_party/vcpkg/installed/x64-windows`

## เริ่มต้นอย่างรวดเร็ว (Quickstart)
1) Clone โปรเจกต์
```
 git clone https://github.com/RisingTonklaman/SIP-record-video.git
 cd SIP-record-video
```

2) รันสคริปต์เตรียมแวดล้อม (ครั้งแรก)
```
 powershell -ExecutionPolicy Bypass -File .\scripts\setup-windows.ps1
```
สิ่งที่สคริปต์ทำ:
- โคลนและบูตสแตรป vcpkg, ติดตั้ง `sdl2:x64-windows`
- โคลน/บิลด์ OpenH264 (ปล่อย DLL แบบ shared)
- (ตัวเลือก) บิลด์ PJSIP จากโฟลเดอร์ `pjsip/pjproject` และแพ็คเกจเข้า `vendor/pjsip/install` — ดีฟอลต์ปิด; ใช้ `-BuildPjsip` หากต้องการบิลด์เอง

3) บิลด์แอป
```
 powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config Release
```
ไบนารีจะอยู่ที่ `build_x64/Release/softphone.exe` และมีการคัดลอก `openh264-8.dll` และ `SDL2.dll` ไปไว้ข้างๆ ให้เรียบร้อย หากยังไม่มี PJSIP ใน `vendor/pjsip/install` สคริปต์จะพยายามบิลด์ pjproject ให้อัตโนมัติ

4) รัน
```
 .\build_x64\Release\softphone.exe <ext> <password> <sip_server>
```

## คำสั่งในโปรแกรม (interpreter)
- `d <ext|sip-uri>`: โทรออก (เปิดวิดีโอ)
- `a`: รับสายเข้า (200 OK พร้อมวิดีโอ)
- `h`: วางสาย
- `w <path.wav>`: เล่น WAV เข้า call
- `tx <gain>`: ปรับ TX level (เช่น 1.10)
- `rx <gain>`: ปรับ RX level (เช่น 1.05)
- `pv [cap_id]`: พรีวิวกล้อง (ดีฟอลต์ 0)
- `pvoff [cap_id]`: ปิดพรีวิว
- `lsvid`: แสดงอุปกรณ์วิดีโอ (capture/render)
- `q`: ออก

## การอัดวิดีโอหน้าต่างปลายทางแบบง่าย (ffmpeg)
โปรแกรมจะตั้งชื่อหน้าต่างวิดีโอปลายทางเป็น `pj-remote-video` และพรีวิวเป็น `pj-local-preview` สามารถอัดวิดีโอ+ผสมเสียงได้ด้วยคำสั่งตัวอย่าง (จาก `command.txt`):
```
ffmpeg -y -f gdigrab -framerate 30 -draw_mouse 0 -i title="pj-remote-video" ^
  -f dshow -i audio="CABLE Output (VB-Audio Virtual Cable)" ^
  -f dshow -i audio="Microphone (AB13X USB Audio)" ^
  -filter_complex "[1:a]aresample=async=1:first_pts=0[a1];[2:a]aresample=async=1:first_pts=0[a2];[a1][a2]amix=inputs=2:duration=longest:normalize=1[a]" ^
  -map 0:v -map "[a]" -c:v libx264 -preset ultrafast -crf 23 -pix_fmt yuv420p ^
  -c:a aac -b:a 128k -ar 48000 -ac 2 -shortest -movflags +faststart "C:\\TonklaSoftphone\\recordings\\remote_mix.mp4"
```
หมายเหตุ: ต้องติดตั้ง ffmpeg เอง และตั้งชื่ออุปกรณ์เสียงตามเครื่องใช้งานจริง

## โฟลเดอร์/ไฟล์สำคัญ
- `CMakeLists.txt`: ตั้งค่า path พื้นฐานให้มองไปยัง `vendor/pjsip/install`, `third_party/openh264`, `third_party/vcpkg`
- `scripts/setup-windows.ps1`: เตรียม vcpkg+SDL2, โคลน/บิลด์ OpenH264, บิลด์ PJSIP
- `scripts/build.ps1`: คอนฟิกและบิลด์โปรเจกต์หลัก (CMake)
- `pjsip/pjproject/pjlib/include/pj/config_site.h`: เปิด video, SDL2, OpenH264 (ใช้เมื่อเลือกบิลด์ PJSIP เอง)

## ปัญหาที่พบบ่อย
- MSBuild ไม่ถูกพบ: ติดตั้ง Visual Studio Build Tools (และ C++ workload)
- SDL2.lib ไม่พบ: ให้รัน `scripts/setup-windows.ps1` เพื่อให้ vcpkg ติดตั้ง SDL2 ใน `third_party/vcpkg`
- openh264-8.dll ไม่พบ: ให้รัน `scripts/setup-windows.ps1` เพื่อบิลด์ OpenH264
- ลิงก์ไม่เจอ PJSIP: ตรวจว่า `vendor/pjsip/install/lib/libpjproject-*.lib` มีอยู่และ `vendor/pjsip/install/include` มี header ครบ

## หมายเหตุด้านไลเซนส์
- PJSIP (pjproject) อยู่ภายใต้ GPLv2 หรือไลเซนส์เชิงพาณิชย์ โปรดตรวจสอบความสอดคล้องของไลเซนส์โค้ดของคุณเมื่อแจกจ่าย
- OpenH264 มีไลเซนส์ BSD-2-Clause พร้อมข้อกำหนดเพิ่มเติมบางประการ
- SDL2 อยู่ภายใต้ Zlib license

หากต้องการให้ผมปรับ CI (GitHub Actions) ให้ build อัตโนมัติ หรือเตรียมสคริปต์สำหรับ Portable ZIP (รวม DLL พร้อมใช้งาน) แจ้งได้ครับ
