# Custom Ubuntu Mono Fonts
# These fonts are custom to the inki project and separate from upstream Waveshare fonts

add_library(fonts_custom STATIC
    # Ubuntu Mono Regular fonts (6pt to 36pt)
    font_ubuntu_mono_6pt.c
    font_ubuntu_mono_7pt.c
    font_ubuntu_mono_8pt.c
    font_ubuntu_mono_9pt.c
    font_ubuntu_mono_10pt.c
    font_ubuntu_mono_11pt.c
    font_ubuntu_mono_12pt.c
    font_ubuntu_mono_14pt.c
    font_ubuntu_mono_16pt.c
    font_ubuntu_mono_18pt.c
    font_ubuntu_mono_18pt_italic.c
    font_ubuntu_mono_18pt_medium.c
    font_ubuntu_mono_20pt.c
    font_ubuntu_mono_22pt.c
    font_ubuntu_mono_24pt.c
    font_ubuntu_mono_26pt.c
    font_ubuntu_mono_28pt.c
    font_ubuntu_mono_36pt.c
    
    # Ubuntu Mono Bold fonts (6pt to 36pt)
    font_ubuntu_mono_6pt_bold.c
    font_ubuntu_mono_7pt_bold.c
    font_ubuntu_mono_8pt_bold.c
    font_ubuntu_mono_9pt_bold.c
    font_ubuntu_mono_10pt_bold.c
    font_ubuntu_mono_11pt_bold.c
    font_ubuntu_mono_12pt_bold.c
    font_ubuntu_mono_14pt_bold.c
    font_ubuntu_mono_16pt_bold.c
    font_ubuntu_mono_18pt_bold.c
    font_ubuntu_mono_20pt_bold.c
    font_ubuntu_mono_22pt_bold.c
    font_ubuntu_mono_24pt_bold.c
    font_ubuntu_mono_26pt_bold.c
    font_ubuntu_mono_28pt_bold.c
    font_ubuntu_mono_36pt_bold.c
)

target_include_directories(fonts_custom PUBLIC . ../Fonts)