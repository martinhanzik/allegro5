option(WANT_FFMPEG "Enable FFMPEG video driver" on)

set(VIDEO_SOURCES
    video.c
    )

set(VIDEO_INCLUDE_FILES allegro5/allegro_video.h)

include_directories(../audio ../image .)

set_our_header_properties(${VIDEO_INCLUDE_FILES})

# The platform conditions are not really necessary but prevent confusing the
# user, e.g. it's pretty weird to get a warning about missing DSound on Unix.

if(WANT_FFMPEG)
    include(AllegroFindFFMPEG)
    if(FFMPEG_FOUND)
        set(SUPPORT_FFMPEG 1)
    endif(FFMPEG_FOUND)
endif(WANT_FFMPEG)

if(SUPPORT_FFMPEG)
    set(ALLEGRO_CFG_VIDEO_FFMPEG 1)
    list(APPEND VIDEO_SOURCES ffmpeg.c)
    set(SUPPORT_VIDEO 1)
    set(VIDEO_LIBRARIES ${FFMPEG_LIBRARIES})
endif(SUPPORT_FFMPEG)

if(NOT SUPPORT_VIDEO)
    message("WARNING: allegro_video wanted but no supported backend found")
    return()
endif(NOT SUPPORT_VIDEO)

# Let examples know that video is supported.
set(SUPPORT_VIDEO 1 PARENT_SCOPE)

add_our_library(allegro_video
    "${VIDEO_SOURCES};${VIDEO_INCLUDE_FILES}"
    "-DALLEGRO_VIDEO_SRC"
    "${ALLEGRO_LINK_WITH};${VIDEO_LIBRARIES}"
    )

set_our_framework_properties(allegro_video AllegroVideo-${ALLEGRO_SOVERSION})

install_our_library(allegro_video)
install_our_headers(${VIDEO_INCLUDE_FILES})

set(VIDEO_LINK_WITH allegro_video PARENT_SCOPE)

#-----------------------------------------------------------------------------#
# vi: set ts=8 sts=4 sw=4 et: