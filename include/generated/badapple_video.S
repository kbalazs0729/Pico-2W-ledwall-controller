// Static wrapper: embeds badapple.bin directly into flash (.rodata).
// Regenerating the video only replaces badapple.bin and the metadata in
// badapple_video.hpp — this file never changes. See make_badapple.py.

.section .rodata
.global badapple_video
.align 4
badapple_video:
.incbin "generated/badapple.bin"
