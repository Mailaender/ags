//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================

#include <stdlib.h>
#include <string.h>
#include "ac/file.h"
#include "util/wgt2allg.h"
#include "media/audio/soundcache.h"
#include "media/audio/audiointernaldefs.h"
#include "util/mutex.h"
#include "util/mutex_lock.h"
#include "util/string.h"

using namespace Common;

sound_cache_entry_t* sound_cache_entries = NULL;
unsigned int sound_cache_counter = 0;

AGS::Engine::Mutex _sound_cache_mutex;


void clear_sound_cache()
{
    AGS::Engine::MutexLock _lock(_sound_cache_mutex);

    if (sound_cache_entries)
    {
        int i;
        for (i = 0; i < psp_audio_cachesize; i++)
        {
            if (sound_cache_entries[i].data)
            {
                free(sound_cache_entries[i].data);
                sound_cache_entries[i].data = NULL;
                free(sound_cache_entries[i].file_name);
                sound_cache_entries[i].file_name = NULL;
                sound_cache_entries[i].reference = 0;
            }
        }
    }
    else
    {
        sound_cache_entries = (sound_cache_entry_t*)malloc(psp_audio_cachesize * sizeof(sound_cache_entry_t));
        memset(sound_cache_entries, 0, psp_audio_cachesize * sizeof(sound_cache_entry_t));
    }
}

void sound_cache_free(char* buffer, bool is_wave)
{
    AGS::Engine::MutexLock _lock(_sound_cache_mutex);

#ifdef SOUND_CACHE_DEBUG
    Debug::Printf("sound_cache_free(%d %d)\n", (unsigned int)buffer, (unsigned int)is_wave);
#endif
    int i;
    for (i = 0; i < psp_audio_cachesize; i++)
    {
        if (sound_cache_entries[i].data == buffer)
        {
            if (sound_cache_entries[i].reference > 0)
                sound_cache_entries[i].reference--;

#ifdef SOUND_CACHE_DEBUG
            Debug::Printf("..decreased reference count of slot %d to %d\n", i, sound_cache_entries[i].reference);
#endif
            return;
        }
    }

#ifdef SOUND_CACHE_DEBUG
    Debug::Printf("..freeing uncached sound\n");
#endif

    // Sound is uncached
    if (i == psp_audio_cachesize)
    {
        if (is_wave)
            destroy_sample((SAMPLE*)buffer);
        else
            free(buffer);
    }
}


char* get_cached_sound(const AssetPath &asset_name, bool is_wave, long* size)
{
	AGS::Engine::MutexLock _lock(_sound_cache_mutex);

#ifdef SOUND_CACHE_DEBUG
    Debug::Printf("get_cached_sound(%s %d)\n", filename, (unsigned int)is_wave);
#endif

    *size = 0;

    int i;
    for (i = 0; i < psp_audio_cachesize; i++)
    {
        if (sound_cache_entries[i].data == NULL)
            continue;

        if (strcmp(asset_name.second, sound_cache_entries[i].file_name) == 0)
        {
#ifdef SOUND_CACHE_DEBUG
            Debug::Printf("..found in slot %d\n", i);
#endif
            sound_cache_entries[i].reference++;
            sound_cache_entries[i].last_used = sound_cache_counter++;
            *size = sound_cache_entries[i].size;

            return sound_cache_entries[i].data;
        }
    }

    // Not found
    PACKFILE *mp3in = NULL;
    SAMPLE* wave = NULL;

    if (is_wave)
    {
        PACKFILE *wavin = PackfileFromAsset(asset_name);
        if (wavin != NULL)
        {
            wave = load_wav_pf(wavin);
            pack_fclose(wavin);
        }
    }  
    else
    {
        mp3in = PackfileFromAsset(asset_name);
        if (mp3in == NULL)
        {
            return NULL;
        }
    }

    // Find free slot
    for (i = 0; i < psp_audio_cachesize; i++)
    {
        if (sound_cache_entries[i].data == NULL)
            break;
    }

    // No free slot?
    if (i == psp_audio_cachesize)
    {
        unsigned int oldest = sound_cache_counter;
        int index = -1;

        for (i = 0; i < psp_audio_cachesize; i++)
        {
            if (sound_cache_entries[i].reference == 0)
            {
                if (sound_cache_entries[i].last_used < oldest)
                {
                    oldest = sound_cache_entries[i].last_used;
                    index = i;
                }
            }
        }

        i = index;
    }

    // Load new file
    char* newdata;

    if (is_wave)
    {
        *size = 0;
        newdata = (char*)wave;
    }
    else
    {
        *size = mp3in->todo;
        newdata = (char *)malloc(*size);

        if (newdata == NULL)
        {
            pack_fclose(mp3in);
            return NULL;
        }

        pack_fread(newdata, *size, mp3in);
        pack_fclose(mp3in);
    }

    if (i == -1)
    {
        // No cache slot empty, return uncached data
#ifdef SOUND_CACHE_DEBUG
        Debug::Printf("..loading uncached\n");
#endif
        return newdata;  
    }
    else
    {
        // Add to cache, free old sound first
#ifdef SOUND_CACHE_DEBUG
        Debug::Printf("..loading cached in slot %d\n", i);
#endif	

        if (sound_cache_entries[i].data) {
            if (sound_cache_entries[i].is_wave)
                destroy_sample((SAMPLE*)sound_cache_entries[i].data);
            else
                free(sound_cache_entries[i].data);
	}
	
        sound_cache_entries[i].size = *size;
        sound_cache_entries[i].data = newdata;

        if (sound_cache_entries[i].file_name)
            free(sound_cache_entries[i].file_name);
        sound_cache_entries[i].file_name = (char*)malloc(strlen(asset_name.second) + 1);
        strcpy(sound_cache_entries[i].file_name, asset_name.second);
        sound_cache_entries[i].reference = 1;
        sound_cache_entries[i].last_used = sound_cache_counter++;
        sound_cache_entries[i].is_wave = is_wave;

        return sound_cache_entries[i].data;	
    }

}
