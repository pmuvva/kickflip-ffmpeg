#include <unistd.h>
#include <fcntl.h>

#include "libavutil/fifo.h"
#include "libavutil/bswap.h"
#include "libavformat/avio.h"


//#include "dash_segmenter.h"
#include <android/log.h>

char * filelocation=NULL;
#define MAXIMUM_FILE_LENGTH 200

#define IO_BUFFER_SIZE (64<<20)
#define FIFO_SIZE (4<<20)       // This is the initial FIFO size and if space
                                // is exhausted while accumulating a segment,
                                // it will be expanded incrementally by this

struct segmenter_state {
    AVFifoBuffer *fifo;
    unsigned int segment_number;
    struct {
        uint32_t size;
        uint8_t type[4];
    } current_box;
    uint8_t found_mdat;
    int file_descriptor;
};

#define LOG_TAG "DASHSegmenterWrap"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define IS_BOX_TYPE(t, a, b, c, d) \
    ((t)[0] == (a) && (t)[1] == (b) && (t)[2] == (c) && (t)[3] == (d))

static void process(struct segmenter_state *state)
{
    // If processing is not currenly in a box...
    if (state->current_box.size == 0) {
        // If there isn't enough data in the FIFO to read 8
        // bytes for the size and type tuple of the next box...
        if (av_fifo_size(state->fifo) < 8) {
            // Return from this processing pass
            return;
        }

        // Read the next 4 bytes from the FIFO and convert from
        // big-endian representation to get the size of the next box
        av_fifo_generic_read(state->fifo, &state->current_box.size, sizeof(uint32_t), NULL);
        state->current_box.size = av_be2ne32(state->current_box.size);

        // Read the next 4 bytes from the FIFO as the type of the next box
        av_fifo_generic_read(state->fifo, state->current_box.type, sizeof(uint32_t), NULL);

        // If this is the initial segment and a file descriptor
        // hasn't been allocated for writing the segment out yet...
        if (state->segment_number == 0 && state->file_descriptor <= 0) {
            // Open the initial segment file for writing
            LOGI("starting initial segment\n");
            char filename[MAXIMUM_FILE_LENGTH];
                        sprintf(filename, "%ssegment_init.mp4",filelocation);
           // state->file_descriptor = open("/storage/emulated/0/Download/MySampleApp/segment_init.mp4", O_WRONLY|O_CREAT|O_TRUNC,
           //                               S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH);
           state->file_descriptor = open(filename, O_WRONLY|O_CREAT|O_TRUNC,
                                                     S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH);
        }
    }

    // If there isn't enough data in the FIFO to read
    // the rest of the payload data for the current box...
    if (av_fifo_size(state->fifo) < (state->current_box.size - 8)) {
        return;
    }

    // If the type of what has just become the current box is "sidx"...
    if (IS_BOX_TYPE(state->current_box.type, 's', 'i', 'd', 'x')) {
        // If the initial segment is being processed or the "mdat"
        // box of a subsequent segment has already been seen...
        if (state->segment_number == 0 || state->found_mdat) {
            // Close the current segment's file
            close(state->file_descriptor);

            // Increment the segment number and reset "mdat" state
            state->segment_number++;
            state->found_mdat = 0;

            // Open the new segment file for writing            
            LOGI("starting segment %d\n", state->segment_number);
            char filename[MAXIMUM_FILE_LENGTH];
            sprintf(filename, "%ssegment_%d.m4s",filelocation, state->segment_number);
            state->file_descriptor = open(filename, O_WRONLY|O_CREAT|O_TRUNC,
                                          S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH);
        }
    }
    // ...else if the type of the next box is "mdat"...
    else if (IS_BOX_TYPE(state->current_box.type, 'm', 'd', 'a', 't')) {
        // Remember that the "mdat" for the current
        // segment being accumulated has been seen
        state->found_mdat = 1;
    }

    // Log box type and size
    LOGI("[%c%c%c%c] size=%d\n",
           state->current_box.type[0], state->current_box.type[1],
           state->current_box.type[2], state->current_box.type[3],
           state->current_box.size - 8);

    // Write the box size and type to the current segment file
    uint32_t size = av_be2ne32(state->current_box.size);
    write(state->file_descriptor, &size, sizeof(uint32_t));
    write(state->file_descriptor, state->current_box.type, sizeof(uint32_t));

    // Read the rest of the payload data for the current box
    // from the FIFO and write it out to the current segment file
    uint8_t *data = av_malloc(state->current_box.size - 8);
    av_fifo_generic_read(state->fifo, data, state->current_box.size - 8, NULL);
    write(state->file_descriptor, data, state->current_box.size - 8);

    // Mark that processing is no longer in a box
    state->current_box.size = 0;

    // Tail recurse back to this function to continue processing
    process(state);
}

static int write_packet(void *opaque, uint8_t *buf, int buf_size)
{
    // Get the FIFO from the opaque processing state
    struct segmenter_state *state = (struct segmenter_state *)opaque;
    AVFifoBuffer *fifo = state->fifo;
    // While the available FIFO space isn't enough...
    while (buf_size > av_fifo_space(fifo)) {
        // Grow the FIFO
        if (av_fifo_grow(fifo, FIFO_SIZE) < 0) {
            return -1;
        }
    }

    // Write the packet data into the FIFO
    av_fifo_generic_write(fifo, buf, buf_size, NULL);

    // Process the FIFO
    process(state);

    // Return success
    return buf_size;
}

int dash_segmenter_avio_open(AVIOContext **context_handle,char*path)
{
	LOGI(" Inside of dash_segmenter_avio_open path is : %s \n",path);
	if(filelocation==NULL){
	    filelocation = (char*)malloc(MAXIMUM_FILE_LENGTH);
	    if(!filelocation){
	        LOGE(" File Memory allocation failed");
	        return -1;
	    }
	    if(path)
	    {
	        strcpy(filelocation,path);

	        char * ptr = strstr(filelocation,"index.fmp4");
	        if(ptr==NULL){
	            LOGI("Wrong Input Path: %s",filelocation);
	            return -1;
	        }
	        else
	        {
	            strcpy(ptr,"");
	            LOGI("Filelocation path is : %s",filelocation);
	        }
	   }
	   else{
	    LOGI(" Wrong file path: %s",path);
	    return -1;
	   }
	}

    // Try to allocate an I/O buffer and if that succeeds...
    uint8_t *io_buffer = av_malloc(IO_BUFFER_SIZE);
    if (io_buffer) {
        // Try to allocate a segmenter state and if that succeeds...
        struct segmenter_state *state = av_malloc(sizeof(*state));
        if (state) {
            // Initialize the segmenter state
            memset(state, 0, sizeof(*state));

            // Try to allocate a segmenter FIFO and if that succeeds...
            state->fifo = av_fifo_alloc(FIFO_SIZE);
            if (state->fifo) {
                // Try to allocate and initialize an I/O context and if that succeeds...
                *context_handle = avio_alloc_context(io_buffer, IO_BUFFER_SIZE, 1,
                                                     state, NULL, write_packet, NULL);
                if (*context_handle) {
                    // Return success
                    LOGI(" Context Handle Success");
                    return 0;
                }else{
                    LOGI(" Context Handle Failed");
                }

                // Free the segmenter FIFO
                av_fifo_free(state->fifo);
            }else{
            LOGE("FIFO Allocation failed");
            }

            // Free the segmenter state
            av_free(state);
        }else{
            LOGE("Segmenter state allocation failed");
        }

        // Free the I/O buffer
        av_free(io_buffer);
    }
    LOGE("io_buffer allocation failed");

    // Return failure
    return AVERROR(ENOMEM);
}
