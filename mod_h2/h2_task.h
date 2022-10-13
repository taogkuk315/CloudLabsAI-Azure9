/* Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __mod_h2__h2_task__
#define __mod_h2__h2_task__

/**
 * A h2_task fakes a HTTP/1.1 request from the data in a HTTP/2 stream 
 * (HEADER+CONT.+DATA) the module recieves.
 *
 * In order to answer a HTTP/2 stream, we want all Apache httpd infrastructure
 * to be involved as usual, as if this stream can as a separate HTTP/1.1
 * request. The basic trickery to do so was derived from google's mod_spdy
 * source. Basically, we fake a new conn_rec object, even with its own
 * socket and give it to ap_process_connection().
 *
 * Since h2_task instances are executed in separate threads, we may have
 * different lifetimes than our h2_stream or h2_session instances. Basically,
 * we would like to be as standalone as possible.
 *
 * Finally, to keep certain connection level filters, such as ourselves and
 * especially mod_ssl ones, from messing with our data, we need a filter
 * of our own to disble those.
 */

struct apr_thread_cond_t;
struct h2_conn;
struct h2_mplx;
struct h2_task;
struct h2_resp_head;
struct h2_worker;

typedef struct h2_task h2_task;

typedef void task_callback(void *ctx, h2_task *task);

struct h2_task {
    /** Links to the rest of the tasks */
    APR_RING_ENTRY(h2_task) link;

    const char *id;
    int stream_id;
    int aborted;
    volatile apr_uint32_t has_started;
    volatile apr_uint32_t has_finished;
    
    struct h2_mplx *mplx;
    struct conn_rec *master;
    apr_pool_t *stream_pool;
    struct h2_conn *conn;
    
    const char *method;
    const char *path;
    const char *authority;
    apr_table_t *headers;
    int serialize_request;

    int input_eos;
    
    struct h2_task_input *input;    /* http/1.1 input data */
    struct h2_task_output *output;  /* response body data */
    struct apr_thread_cond_t *io;   /* optional condition to wait for io on */
    
    task_callback *on_finished;
    void *ctx_finished;
};

/**
 * The magic pointer value that indicates the head of a h2_task list
 * @param  b The task list
 * @return The magic pointer value
 */
#define H2_TASK_LIST_SENTINEL(b)	APR_RING_SENTINEL((b), h2_task, link)

/**
 * Determine if the task list is empty
 * @param b The list to check
 * @return true or false
 */
#define H2_TASK_LIST_EMPTY(b)	APR_RING_EMPTY((b), h2_task, link)

/**
 * Return the first task in a list
 * @param b The list to query
 * @return The first task in the list
 */
#define H2_TASK_LIST_FIRST(b)	APR_RING_FIRST(b)

/**
 * Return the last task in a list
 * @param b The list to query
 * @return The last task int he list
 */
#define H2_TASK_LIST_LAST(b)	APR_RING_LAST(b)

/**
 * Insert a single task at the front of a list
 * @param b The list to add to
 * @param e The task to insert
 */
#define H2_TASK_LIST_INSERT_HEAD(b, e) do {				\
    h2_task *ap__b = (e);                                        \
    APR_RING_INSERT_HEAD((b), ap__b, h2_task, link);	\
} while (0)

/**
 * Insert a single task at the end of a list
 * @param b The list to add to
 * @param e The task to insert
 */
#define H2_TASK_LIST_INSERT_TAIL(b, e) do {				\
    h2_task *ap__b = (e);					\
    APR_RING_INSERT_TAIL((b), ap__b, h2_task, link);	\
} while (0)

/**
 * Get the next task in the list
 * @param e The current task
 * @return The next task
 */
#define H2_TASK_NEXT(e)	APR_RING_NEXT((e), link)
/**
 * Get the previous task in the list
 * @param e The current task
 * @return The previous task
 */
#define H2_TASK_PREV(e)	APR_RING_PREV((e), link)

/**
 * Remove a task from its list
 * @param e The task to remove
 */
#define H2_TASK_REMOVE(e)	APR_RING_REMOVE((e), link)


h2_task *h2_task_create(long session_id, int stream_id, conn_rec *master,
                        apr_pool_t *pool, struct h2_mplx *mplx);

void h2_task_set_request(h2_task *task, const char *method, const char *path, 
                         const char *authority, apr_table_t *headers, int eos);

apr_status_t h2_task_destroy(h2_task *task);

apr_status_t h2_task_prep_conn(h2_task *task);

apr_status_t h2_task_do(h2_task *task, struct h2_worker *worker);
apr_status_t h2_task_process_request(h2_task *task);

void h2_task_abort(h2_task *task);
int h2_task_is_aborted(h2_task *task);
void h2_task_interrupt(h2_task *task);

int h2_task_has_started(h2_task *task);
void h2_task_set_started(h2_task *task, struct apr_thread_cond_t *cond);
int h2_task_has_finished(h2_task *task);
void h2_task_set_finished(h2_task *task);

void h2_task_on_finished(h2_task *task, task_callback *cb, void *cb_ctx);

const char *h2_task_get_id(h2_task *task);

void h2_task_register_hooks(void);

int h2_task_pre_conn(h2_task *task, conn_rec *c);

struct apr_thread_cond_t *h2_task_get_io_cond(h2_task *task);

#endif /* defined(__mod_h2__h2_task__) */
