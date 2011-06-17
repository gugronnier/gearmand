/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Gearmand client and server library.
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/
 *  Copyright (C) 2008 Brian Aker, Eric Day
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following disclaimer
 *  in the documentation and/or other materials provided with the
 *  distribution.
 *
 *      * The names of its contributors may not be used to endorse or
 *  promote products derived from this software without specific prior
 *  written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"

#if defined(NDEBUG)
# undef NDEBUG
#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define GEARMAN_CORE
#include <libgearman/gearman.h>

#include <libtest/server.h>
#include <libtest/test.h>
#include <libtest/worker.h>

#define CLIENT_TEST_PORT 32123

#define NAMESPACE_KEY "foo123"

#define WORKER_FUNCTION_NAME "client_test"
#define WORKER_CHUNKED_FUNCTION_NAME "reverse_test"
#define WORKER_UNIQUE_FUNCTION_NAME "unique_test"
#define WORKER_SPLIT_FUNCTION_NAME "split_worker"

#include <tests/do.h>
#include <tests/server_options.h>
#include <tests/do_background.h>
#include <tests/execute.h>
#include <tests/gearman_client_do_job_handle.h>
#include <tests/gearman_execute_map_reduce.h>
#include <tests/protocol.h>
#include <tests/task.h>
#include <tests/unique.h>
#include <tests/workers.h>

#ifndef __INTEL_COMPILER
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

struct client_test_st
{
  gearman_client_st *_client;
  bool _clone;
  pid_t gearmand_pid;
  struct worker_handle_st *completion_worker;
  struct worker_handle_st *chunky_worker;
  struct worker_handle_st *unique_check;
  struct worker_handle_st *split_worker;
  struct worker_handle_st *namespace_completion_worker;
  struct worker_handle_st *namespace_chunky_worker;
  struct worker_handle_st *namespace_split_worker;
  struct worker_handle_st* increment_reset_worker[10]; 
  const char *_worker_name;

  client_test_st() :
    _clone(true),
    gearmand_pid(-1),
    completion_worker(NULL),
    chunky_worker(NULL),
    unique_check(NULL),
    split_worker(NULL),
    namespace_completion_worker(NULL),
    _worker_name(WORKER_FUNCTION_NAME)
  { 
    if (not (_client= gearman_client_create(NULL)))
    {
      abort(); // This would only happen from a programming error
    }

  }

  ~client_test_st()
  {
    test_gearmand_stop(gearmand_pid);
    test_worker_stop(completion_worker);
    test_worker_stop(chunky_worker);
    test_worker_stop(unique_check);
    test_worker_stop(split_worker);
    test_worker_stop(namespace_completion_worker);
    test_worker_stop(namespace_chunky_worker);
    test_worker_stop(namespace_split_worker);

    for (uint32_t x= 0; x < 10; x++)
    {
      test_worker_stop(increment_reset_worker[x]);
    }
    gearman_client_free(_client);
  }

  const char *worker_name() const
  {
    return _worker_name;
  }

  void set_worker_name(const char *arg)
  {
    _worker_name= arg;
  }

  void set_clone(bool arg)
  {
    _clone= arg;
  }

  bool clone() const
  {
    return _clone;
  }

  gearman_client_st *client()
  {
    return _client;
  }

  void reset_client()
  {
    gearman_client_free(_client);
    _client= gearman_client_create(NULL);
  }
};

#ifndef __INTEL_COMPILER
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

/**
  @note Just here until I fix libhashkit.
*/
static uint32_t internal_generate_hash(const char *key, size_t key_length)
{
  const char *ptr= key;
  uint32_t value= 0;

  while (key_length--)
  {
    uint32_t val= (uint32_t) *ptr++;
    value += val;
    value += (value << 10);
    value ^= (value >> 6);
  }
  value += (value << 3);
  value ^= (value >> 11);
  value += (value << 15);

  return value == 0 ? 1 : (uint32_t) value;
}

/* Prototypes */
void *client_test_temp_worker(gearman_job_st *job, void *context,
                              size_t *result_size, gearman_return_t *ret_ptr);
void *world_create(test_return_t *error);
test_return_t world_destroy(void *object);


static void *client_thread(void *object)
{
  (void)object;
  gearman_client_st client;
  gearman_client_st *client_ptr;
  size_t result_size;

  client_ptr= gearman_client_create(&client);

  if (client_ptr == NULL)
    abort(); // This would be pretty bad.

  gearman_return_t rc= gearman_client_add_server(&client, NULL, CLIENT_TEST_PORT);
  if (gearman_failed(rc))
  {
    pthread_exit(0);
  }

  gearman_client_set_timeout(&client, 400);
  for (size_t x= 0; x < 5; x++)
  {
    (void) gearman_client_do(&client, "client_test_temp", NULL, NULL, 0,
                             &result_size, &rc);

  }
  gearman_client_free(client_ptr);

  pthread_exit(0);
}

static test_return_t init_test(void *)
{
  gearman_client_st client;

  test_truth(gearman_client_create(&client));

  gearman_client_free(&client);

  return TEST_SUCCESS;
}

static test_return_t allocation_test(void *)
{
  gearman_client_st *client;

  test_truth(client= gearman_client_create(NULL));

  gearman_client_free(client);

  return TEST_SUCCESS;
}

static test_return_t clone_test(void *object)
{
  const gearman_client_st *from= (gearman_client_st *)object;
  gearman_client_st *from_with_host;
  gearman_client_st *client;

  client= gearman_client_clone(NULL, NULL);

  test_truth(client);
  test_truth(client->options.allocated);

  gearman_client_free(client);

  client= gearman_client_clone(NULL, from);
  test_truth(client);
  gearman_client_free(client);

  from_with_host= gearman_client_create(NULL);
  test_truth(from_with_host);
  gearman_client_add_server(from_with_host, "127.0.0.1", 12345);

  client= gearman_client_clone(NULL, from_with_host);
  test_truth(client);

  test_truth(client->universal.con_list);
  test_truth(gearman_client_compare(client, from_with_host));

  gearman_client_free(client);
  gearman_client_free(from_with_host);

  return TEST_SUCCESS;
}

static test_return_t option_test(void *)
{
  gearman_client_st *gear;
  gearman_client_options_t default_options;

  gear= gearman_client_create(NULL);
  test_truth(gear);
  { // Initial Allocated, no changes
    test_truth(gear->options.allocated);
    test_false(gear->options.non_blocking);
    test_false(gear->options.unbuffered_result);
    test_false(gear->options.no_new);
    test_false(gear->options.free_tasks);
  }

  /* Set up for default options */
  default_options= gearman_client_options(gear);

  /*
    We take the basic options, and push
    them back in. See if we change anything.
  */
  gearman_client_set_options(gear, default_options);
  { // Initial Allocated, no changes
    test_truth(gear->options.allocated);
    test_false(gear->options.non_blocking);
    test_false(gear->options.unbuffered_result);
    test_false(gear->options.no_new);
    test_false(gear->options.free_tasks);
  }

  /*
    We will trying to modify non-mutable options (which should not be allowed)
  */
  {
    gearman_client_remove_options(gear, GEARMAN_CLIENT_ALLOCATED);
    { // Initial Allocated, no changes
      test_truth(gear->options.allocated);
      test_false(gear->options.non_blocking);
      test_false(gear->options.unbuffered_result);
      test_false(gear->options.no_new);
      test_false(gear->options.free_tasks);
    }
    gearman_client_remove_options(gear, GEARMAN_CLIENT_NO_NEW);
    { // Initial Allocated, no changes
      test_truth(gear->options.allocated);
      test_false(gear->options.non_blocking);
      test_false(gear->options.unbuffered_result);
      test_false(gear->options.no_new);
      test_false(gear->options.free_tasks);
    }
  }

  /*
    We will test modifying GEARMAN_CLIENT_NON_BLOCKING in several manners.
  */
  {
    gearman_client_remove_options(gear, GEARMAN_CLIENT_NON_BLOCKING);
    { // GEARMAN_CLIENT_NON_BLOCKING set to default, by default.
      test_truth(gear->options.allocated);
      test_false(gear->options.non_blocking);
      test_false(gear->options.unbuffered_result);
      test_false(gear->options.no_new);
      test_false(gear->options.free_tasks);
    }
    gearman_client_add_options(gear, GEARMAN_CLIENT_NON_BLOCKING);
    { // GEARMAN_CLIENT_NON_BLOCKING set to default, by default.
      test_truth(gear->options.allocated);
      test_truth(gear->options.non_blocking);
      test_false(gear->options.unbuffered_result);
      test_false(gear->options.no_new);
      test_false(gear->options.free_tasks);
    }
    gearman_client_set_options(gear, GEARMAN_CLIENT_NON_BLOCKING);
    { // GEARMAN_CLIENT_NON_BLOCKING set to default, by default.
      test_truth(gear->options.allocated);
      test_truth(gear->options.non_blocking);
      test_false(gear->options.unbuffered_result);
      test_false(gear->options.no_new);
      test_false(gear->options.free_tasks);
    }
    gearman_client_set_options(gear, GEARMAN_CLIENT_UNBUFFERED_RESULT);
    { // Everything is now set to false except GEARMAN_CLIENT_UNBUFFERED_RESULT, and non-mutable options
      test_truth(gear->options.allocated);
      test_false(gear->options.non_blocking);
      test_truth(gear->options.unbuffered_result);
      test_false(gear->options.no_new);
      test_false(gear->options.free_tasks);
    }
    /*
      Reset options to default. Then add an option, and then add more options. Make sure
      the options are all additive.
    */
    {
      gearman_client_set_options(gear, default_options);
      { // See if we return to defaults
        test_truth(gear->options.allocated);
        test_false(gear->options.non_blocking);
        test_false(gear->options.unbuffered_result);
        test_false(gear->options.no_new);
        test_false(gear->options.free_tasks);
      }
      gearman_client_add_options(gear, GEARMAN_CLIENT_FREE_TASKS);
      { // All defaults, except timeout_return
        test_truth(gear->options.allocated);
        test_false(gear->options.non_blocking);
        test_false(gear->options.unbuffered_result);
        test_false(gear->options.no_new);
        test_truth(gear->options.free_tasks);
      }
      gearman_client_add_options(gear, (gearman_client_options_t)(GEARMAN_CLIENT_NON_BLOCKING|GEARMAN_CLIENT_UNBUFFERED_RESULT));
      { // GEARMAN_CLIENT_NON_BLOCKING set to default, by default.
        test_truth(gear->options.allocated);
        test_truth(gear->options.non_blocking);
        test_truth(gear->options.unbuffered_result);
        test_false(gear->options.no_new);
        test_truth(gear->options.free_tasks);
      }
    }
    /*
      Add an option, and then replace with that option plus a new option.
    */
    {
      gearman_client_set_options(gear, default_options);
      { // See if we return to defaults
        test_truth(gear->options.allocated);
        test_false(gear->options.non_blocking);
        test_false(gear->options.unbuffered_result);
        test_false(gear->options.no_new);
        test_false(gear->options.free_tasks);
      }
      gearman_client_add_options(gear, GEARMAN_CLIENT_FREE_TASKS);
      { // All defaults, except timeout_return
        test_truth(gear->options.allocated);
        test_false(gear->options.non_blocking);
        test_false(gear->options.unbuffered_result);
        test_false(gear->options.no_new);
        test_truth(gear->options.free_tasks);
      }
      gearman_client_add_options(gear, (gearman_client_options_t)(GEARMAN_CLIENT_FREE_TASKS|GEARMAN_CLIENT_UNBUFFERED_RESULT));
      { // GEARMAN_CLIENT_NON_BLOCKING set to default, by default.
        test_truth(gear->options.allocated);
        test_false(gear->options.non_blocking);
        test_truth(gear->options.unbuffered_result);
        test_false(gear->options.no_new);
        test_truth(gear->options.free_tasks);
      }
    }
  }

  gearman_client_free(gear);

  return TEST_SUCCESS;
}

static test_return_t echo_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  test_truth(client);

  gearman_string_t value= { gearman_literal_param("This is my echo test") };

  test_compare(GEARMAN_SUCCESS, gearman_client_echo(client, gearman_string_param(value)));

  return TEST_SUCCESS;
}

static test_return_t submit_job_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  const char *worker_function= (const char *)gearman_client_context(client);
  gearman_string_t value= { gearman_literal_param("submit_job_test") };

  size_t result_length;
  gearman_return_t rc;
  void *job_result= gearman_client_do(client, worker_function, NULL, gearman_string_param(value), &result_length, &rc);

  test_compare_got(GEARMAN_SUCCESS, rc, gearman_client_error(client) ? gearman_client_error(client) : gearman_strerror(rc));
  test_truth(job_result);
  test_compare(gearman_size(value), result_length);

  test_memcmp(gearman_c_str(value), job_result, gearman_size(value));

  free(job_result);

  return TEST_SUCCESS;
}

static test_return_t submit_null_job_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;

  test_truth(client);

  const char *worker_function= (const char *)gearman_client_context(client);
  test_truth(worker_function);

  size_t result_length;
  gearman_return_t rc;
  void *job_result= gearman_client_do(client, worker_function, NULL, NULL, 0,
                                      &result_length, &rc);
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_client_error(client));
  test_compare(0, result_length);
  test_false(job_result);

  return TEST_SUCCESS;
}

static test_return_t submit_exception_job_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  test_truth(client);

  const char *worker_function= (const char *)gearman_client_context(client);
  test_truth(worker_function);

  size_t result_length;
  gearman_return_t rc;
  void *job_result= gearman_client_do(client, worker_function, NULL,
                                      gearman_literal_param("exception"),
                                      &result_length, &rc);
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_client_error(client) ? gearman_client_error(client) : gearman_strerror(rc));
  test_memcmp("exception", job_result, result_length);
  free(job_result);

  return TEST_SUCCESS;
}

static test_return_t submit_warning_job_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  test_truth(client);

  const char *worker_function= (const char *)gearman_client_context(client);
  test_truth(worker_function);

  size_t result_length;
  gearman_return_t rc;
  void *job_result= gearman_client_do(client, worker_function, NULL,
                                      gearman_literal_param("warning"),
                                      &result_length, &rc);
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_client_error(client) ? gearman_client_error(client) : gearman_strerror(rc));
  test_memcmp("warning", job_result, result_length);
  free(job_result);

  return TEST_SUCCESS;
}

static test_return_t submit_fail_job_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  test_truth(client);


  const char *worker_function= (const char *)gearman_client_context(client);
  test_truth(worker_function);

  size_t result_length;
  gearman_return_t rc;
  void *job_result= gearman_client_do(client, worker_function, NULL, "fail", 4,
                                      &result_length, &rc);
  test_compare_got(GEARMAN_WORK_FAIL, rc, gearman_client_error(client));
  test_false(job_result);
  test_false(result_length);

  return TEST_SUCCESS;
}

static test_return_t submit_multiple_do(void *object)
{
  for (uint32_t x= 0; x < 100 /* arbitrary */; x++)
  {
    uint32_t option= random() %3;

    switch (option)
    {
    case 0:
      test_compare(TEST_SUCCESS, submit_null_job_test(object));
      break;
    case 1:
      test_compare(TEST_SUCCESS, submit_job_test(object));
      break;
    default:
    case 2:
      test_compare(TEST_SUCCESS, submit_fail_job_test(object));
      break;
    }
  }

  return TEST_SUCCESS;
}

static test_return_t gearman_client_job_status_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  test_truth(client);

  gearman_string_t value= { gearman_literal_param("background_test") };

  const char *worker_function= (const char *)gearman_client_context(client);
  test_truth(worker_function);

  gearman_job_handle_t job_handle;
  test_compare_got(GEARMAN_SUCCESS,
                   gearman_client_do_background(client, worker_function, NULL, gearman_string_param(value), job_handle), 
                   gearman_client_error(client));

  gearman_return_t ret;
  bool is_known;
  do
  {
    bool is_running;
    uint32_t numerator;
    uint32_t denominator;

    test_compare_got(GEARMAN_SUCCESS,
                     ret= gearman_client_job_status(client, job_handle, &is_known, &is_running, &numerator, &denominator),
                     gearman_client_error(client));
  } while (gearman_continue(ret) and is_known);

  return TEST_SUCCESS;
}

static test_return_t gearman_client_job_status_with_return(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  test_truth(client);

  gearman_string_t value= { gearman_literal_param("background_test") };

  const char *worker_function= (const char *)gearman_client_context(client);
  test_truth(worker_function);

  gearman_job_handle_t job_handle;
  test_compare_got(GEARMAN_SUCCESS,
                   gearman_client_do_background(client, worker_function, NULL, gearman_string_param(value), job_handle), 
                   gearman_client_error(client));

  gearman_return_t ret;
  do
  {
    uint32_t numerator;
    uint32_t denominator;

    ret= gearman_client_job_status(client, job_handle, NULL, NULL, &numerator, &denominator);
  } while (gearman_continue(ret));
  test_compare(GEARMAN_SUCCESS, ret);

  return TEST_SUCCESS;
}

static test_return_t background_failure_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  gearman_job_handle_t job_handle;
  bool is_known;
  bool is_running;
  uint32_t numerator;
  uint32_t denominator;

  gearman_return_t rc= gearman_client_do_background(client, "does_not_exist", NULL,
                                                    gearman_literal_param("background_failure_test"),
                                                    job_handle);
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_client_error(client));

  do {
    rc= gearman_client_job_status(client, job_handle, &is_known, &is_running,
                                  &numerator, &denominator);
    test_true(is_known == true and is_running == false and numerator == 0 and denominator == 0);
  } while (gearman_continue(rc)); // We do not test for is_known since the server will keep the job around until a worker comes along
  test_compare(GEARMAN_SUCCESS, rc);

  return TEST_SUCCESS;
}

static test_return_t add_servers_test(void *)
{
  gearman_client_st client, *client_ptr;

  client_ptr= gearman_client_create(&client);
  test_truth(client_ptr);

  gearman_return_t rc;
  rc= gearman_client_add_servers(&client, "127.0.0.1:4730,localhost");
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_strerror(rc));

  rc= gearman_client_add_servers(&client, "old_jobserver:7003,broken:12345");
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_strerror(rc));

  gearman_client_free(&client);

  return TEST_SUCCESS;
}

static test_return_t hostname_resolution(void *)
{
  gearman_client_st *client= gearman_client_create(NULL);
  test_truth(client);

  test_compare(GEARMAN_SUCCESS,
               gearman_client_add_servers(client, "exist.gearman.info"));

  test_compare(GEARMAN_GETADDRINFO,
               gearman_client_echo(client, gearman_literal_param("foo")));

  gearman_client_free(client);

  return TEST_SUCCESS;
}

static test_return_t bug_518512_test(void *)
{
  gearman_client_st client;
  size_t result_size;

  test_truth(gearman_client_create(&client));

  gearman_return_t rc;
  test_compare(GEARMAN_SUCCESS,
               gearman_client_add_server(&client, NULL, CLIENT_TEST_PORT));

  gearman_client_set_timeout(&client, 100);
  void *result= gearman_client_do(&client, "client_test_temp", NULL, NULL, 0,
                                  &result_size, &rc);
  test_compare_got(GEARMAN_TIMEOUT, rc, gearman_strerror(rc));
  test_false(result);
  test_compare(0, result_size);

  struct worker_handle_st *completion_worker= test_worker_start(CLIENT_TEST_PORT, "client_test_temp",
                                                                client_test_temp_worker, NULL, gearman_worker_options_t());

  gearman_client_set_timeout(&client, -1);
  result= gearman_client_do(&client, "client_test_temp", NULL, NULL, 0,
                            &result_size, &rc);
  test_true_got(rc != GEARMAN_TIMEOUT, gearman_strerror(rc));
  (void)result;

  test_worker_stop(completion_worker);
  gearman_client_free(&client);

  return TEST_SUCCESS;
}

#define NUMBER_OF_WORKERS 2

static test_return_t loop_test(void *)
{
  void *unused;
  pthread_attr_t attr;

  pthread_t one;
  pthread_t two;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  struct worker_handle_st *handles[NUMBER_OF_WORKERS];
  for (size_t x= 0; x < NUMBER_OF_WORKERS; x++)
  {
    handles[x]= test_worker_start(CLIENT_TEST_PORT, "client_test_temp",
                                  client_test_temp_worker, NULL, gearman_worker_options_t());
  }

  pthread_create(&one, &attr, client_thread, NULL);
  pthread_create(&two, &attr, client_thread, NULL);

  pthread_join(one, &unused);
  pthread_join(two, &unused);

  for (size_t x= 0; x < NUMBER_OF_WORKERS; x++)
  {
    test_worker_stop(handles[x]);
  }

  pthread_attr_destroy(&attr);

  return TEST_SUCCESS;
}

static test_return_t regression_768317_test(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;

  test_true(client);
  size_t result_length;
  gearman_return_t rc;
  char *job_result= (char*)gearman_client_do(client, "increment_reset_worker", 
                                             NULL, 
                                             gearman_literal_param("reset"),
                                             &result_length, &rc);
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_strerror(rc));
  test_false(job_result);

  // Check to see that the task ran just once
  job_result= (char*)gearman_client_do(client, "increment_reset_worker", 
                                       NULL, 
                                       gearman_literal_param("10"),
                                       &result_length, &rc);
  test_compare_got(GEARMAN_SUCCESS, rc, gearman_client_error(client));
  test_truth(job_result);
  long count= strtol(job_result, (char **)NULL, 10);
  test_compare(10, count);
  free(job_result);

  // Check to see that the task ran just once out of the bg queue
  {
    gearman_job_handle_t job_handle;
    rc= gearman_client_do_background(client,
                                     "increment_reset_worker",
                                     NULL,
                                     gearman_literal_param("14"),
                                     job_handle);
    test_compare(GEARMAN_SUCCESS, rc);

    bool is_known;
    do {
      rc= gearman_client_job_status(client, job_handle, &is_known, NULL, NULL, NULL);
    }  while (gearman_continue(rc) or is_known);
    test_compare(GEARMAN_SUCCESS, rc);

    job_result= (char*)gearman_client_do(client, "increment_reset_worker", 
                                         NULL, 
                                         gearman_literal_param("10"),
                                         &result_length, &rc);
    test_compare(GEARMAN_SUCCESS, rc);
    test_truth(job_result);
    count= atol(job_result);
    test_compare(34, count);
    free(job_result);
  }

  return TEST_SUCCESS;
}

static test_return_t submit_log_failure(void *object)
{
  gearman_client_st *client= (gearman_client_st *)object;
  test_truth(client);
  gearman_string_t value= { gearman_literal_param("submit_log_failure") };

  const char *worker_function= (const char *)gearman_client_context(client);
  test_truth(worker_function);

  size_t result_length;
  gearman_return_t rc;
  void *job_result= gearman_client_do(client, worker_function, NULL, 
                                      gearman_string_param(value),
                                      &result_length, &rc);
  test_compare(GEARMAN_NO_SERVERS, rc);
  test_false(job_result);
  test_compare(0, result_length);

  return TEST_SUCCESS;
}

static void log_counter(const char *line, gearman_verbose_t verbose,
                        void *context)
{
  uint32_t *counter= (uint32_t *)context;

  (void)verbose;
  (void)line;

  *counter= *counter + 1;
}

static test_return_t strerror_count(void *)
{
  test_compare((int)GEARMAN_MAX_RETURN, 51);

  return TEST_SUCCESS;
}

#undef MAKE_NEW_STRERROR

static char * make_number(uint32_t expected, uint32_t got)
{
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "Expected %uU, got %uU", expected, got);

  return strdup(buffer);
}

static test_return_t strerror_strings(void *)
{
  uint32_t values[]= {
    2723107532U, 1294272985U, 949848612U, 646434617U, 
    2273096667U, 3411376012U, 978198404U, 2644287234U, 
    1762137345U, 1727436301U, 1103093142U, 2958899803U, 
    3844590487U, 3520316764U, 3288532333U, 697573278U, 
    2328987341U, 1321921098U, 1475770122U, 4011631587U, 
    2468981698U, 2935753385U, 884320816U, 3006705975U, 
    2840498210U, 2953034368U, 501858685U, 1635925784U, 
    880765771U, 15612712U, 1489284002U, 2968621609U, 
    79936336U, 3059874010U, 3562217099U, 13337402U, 
    132823274U, 3950859856U, 237150774U, 290535510U, 
    2101976744U, 2262698284U, 3182950564U, 2391595326U, 
    1764731897U, 3485422815U, 99607280U, 2348849961U, 
    607991020U, 1597605008U, 1377573125U };

  for (int rc= GEARMAN_SUCCESS; rc < GEARMAN_MAX_RETURN; rc++)
  {
    uint32_t hash_val;
    const char *msg=  gearman_strerror((gearman_return_t)rc);
    hash_val= internal_generate_hash(msg, strlen(msg));
    test_compare_got(values[rc], hash_val, make_number(values[rc], hash_val));
  }

  return TEST_SUCCESS;
}

static uint32_t global_counter;

static test_return_t pre_chunk(void *object)
{
  client_test_st *all= (client_test_st *)object;

  all->set_worker_name(WORKER_CHUNKED_FUNCTION_NAME);

  return TEST_SUCCESS;
}

static test_return_t pre_namespace(void *object)
{
  client_test_st *all= (client_test_st *)object;

  gearman_client_set_namespace(all->client(), NAMESPACE_KEY, strlen(NAMESPACE_KEY));

  return TEST_SUCCESS;
}

static test_return_t pre_unique(void *object)
{
  client_test_st *all= (client_test_st *)object;

  all->set_worker_name(WORKER_UNIQUE_FUNCTION_NAME);

  return TEST_SUCCESS;
}

static test_return_t post_function_reset(void *object)
{
  client_test_st *all= (client_test_st *)object;

  all->set_worker_name(WORKER_FUNCTION_NAME);
  gearman_client_set_namespace(all->client(), 0, 0);

  return TEST_SUCCESS;
}

static test_return_t pre_logging(void *object)
{
  client_test_st *all= (client_test_st *)object;
  gearman_log_fn *func= log_counter;
  global_counter= 0;
  all->reset_client();
  all->set_clone(false);

  gearman_client_set_log_fn(all->client(), func, &global_counter, GEARMAN_VERBOSE_MAX);

  return TEST_SUCCESS;
}

static test_return_t post_logging(void *)
{
  test_truth(global_counter);

  return TEST_SUCCESS;
}

void *client_test_temp_worker(gearman_job_st *, void *,
                              size_t *result_size, gearman_return_t *ret_ptr)
{
  *result_size= 0;
  *ret_ptr= GEARMAN_SUCCESS;
  return NULL;
}

void *world_create(test_return_t *error)
{
  client_test_st *test= new client_test_st();
  pid_t gearmand_pid;

  /**
   *  @TODO We cast this to char ** below, which is evil. We need to do the
   *  right thing
   */
  const char *argv[1]= { "client_gearmand" };

  if (not test)
  {
    *error= TEST_MEMORY_ALLOCATION_FAILURE;
    return NULL;
  }

  /**
    We start up everything before we allocate so that we don't have to track memory in the forked process.
  */
  gearmand_pid= test_gearmand_start(CLIENT_TEST_PORT, 1, argv);
  
  if (gearmand_pid == -1)
  {
    *error= TEST_FAILURE;
    return NULL;
  }

  test->completion_worker= test_worker_start(CLIENT_TEST_PORT, WORKER_FUNCTION_NAME, echo_or_react_worker, NULL, gearman_worker_options_t());
  test->chunky_worker= test_worker_start(CLIENT_TEST_PORT, WORKER_CHUNKED_FUNCTION_NAME, echo_or_react_chunk_worker, NULL, gearman_worker_options_t());
  test->unique_check= test_worker_start(CLIENT_TEST_PORT, WORKER_UNIQUE_FUNCTION_NAME, unique_worker, NULL, GEARMAN_WORKER_GRAB_UNIQ);
  test->split_worker= test_worker_start_with_reducer(CLIENT_TEST_PORT, NULL, WORKER_SPLIT_FUNCTION_NAME, split_worker, cat_aggregator_fn,  NULL, GEARMAN_WORKER_GRAB_ALL);

  test->namespace_completion_worker= test_worker_start_with_namespace(CLIENT_TEST_PORT, WORKER_FUNCTION_NAME, echo_or_react_worker, NULL, NAMESPACE_KEY, gearman_worker_options_t());
  test->namespace_chunky_worker= test_worker_start_with_namespace(CLIENT_TEST_PORT, WORKER_CHUNKED_FUNCTION_NAME, echo_or_react_worker, NULL, NAMESPACE_KEY, gearman_worker_options_t());
  test->namespace_split_worker= test_worker_start_with_reducer(CLIENT_TEST_PORT, NAMESPACE_KEY, WORKER_SPLIT_FUNCTION_NAME, split_worker, cat_aggregator_fn,  NULL, GEARMAN_WORKER_GRAB_ALL);

  for (uint32_t x= 0; x < 10; x++)
  {
    test->increment_reset_worker[x]= test_worker_start(CLIENT_TEST_PORT, 
                                                       "increment_reset_worker", increment_reset_worker, 
                                                       NULL, gearman_worker_options_t());
  }

  test->gearmand_pid= gearmand_pid;

  if (gearman_failed(gearman_client_add_server(test->client(), NULL, CLIENT_TEST_PORT)))
  {
    *error= TEST_FAILURE;
    return NULL;
  }

  *error= TEST_SUCCESS;

  return (void *)test;
}


test_return_t world_destroy(void *object)
{
  client_test_st *test= (client_test_st *)object;
  delete test;

  return TEST_SUCCESS;
}


test_st tests[] ={
  {"init", 0, init_test },
  {"allocation", 0, allocation_test },
  {"clone_test", 0, clone_test },
  {"echo", 0, echo_test },
  {"options", 0, option_test },
  {"submit_job", 0, submit_job_test },
  {"submit_null_job", 0, submit_null_job_test },
  {"submit_fail_job", 0, submit_fail_job_test },
  {"exception", 0, submit_exception_job_test },
  {"warning", 0, submit_warning_job_test },
  {"submit_multiple_do", 0, submit_multiple_do },
  {"gearman_client_job_status()", 0, gearman_client_job_status_test },
  {"gearman_client_job_status() with gearman_return_t", 0, gearman_client_job_status_with_return },
  {"background_failure", 0, background_failure_test },
  {"add_servers", 0, add_servers_test },
  {"bug_518512_test", 0, bug_518512_test },
  {"gearman_client_add_servers(GEARMAN_GETADDRINFO)", 0, hostname_resolution },
  {"loop_test", 0, loop_test },
  {0, 0, 0}
};

test_st gearman_command_t_tests[] ={
  {"gearman_command_t", 0, check_gearman_command_t },
  {0, 0, 0}
};


test_st tests_log[] ={
  {"submit_log_failure", 0, submit_log_failure },
  {0, 0, 0}
};

test_st gearman_strerror_tests[] ={
  {"count", 0, strerror_count },
  {"strings", 0, strerror_strings },
  {0, 0, 0}
};

test_st unique_tests[] ={
  {"compare sent unique", 0, unique_compare_test },
  {0, 0, 0}
};

test_st regression_tests[] ={
  {"lp:768317", 0, regression_768317_test },
  {0, 0, 0}
};

test_st gearman_client_do_tests[] ={
  {"gearman_client_do() fail huge unique", 0, gearman_client_do_huge_unique },
  {"gearman_client_do() with active background task", 0, gearman_client_do_with_active_background_task },
  {0, 0, 0}
};

test_st gearman_execute_tests[] ={
  {"gearman_execute()", 0, gearman_execute_test },
  {"gearman_execute(GEARMAN_WORK_FAIL)", 0, gearman_execute_fail_test },
  {"gearman_execute() epoch", 0, gearman_execute_epoch_test },
  {"gearman_execute() epoch and test gearman_job_handle_t", 0, gearman_execute_epoch_check_job_handle_test },
  {"gearman_execute(GEARMAN_TIMEOUT)", 0, gearman_execute_timeout_test },
  {"gearman_execute() background", 0, gearman_execute_bg_test },
  {"gearman_execute() multiple background", 0, gearman_execute_multile_bg_test },
  {0, 0, 0}
};

test_st gearman_client_do_background_tests[] ={
  {"gearman_client_do_background()", 0, gearman_client_do_background_basic },
  {"gearman_client_do_high_background()", 0, gearman_client_do_high_background_basic },
  {"gearman_client_do_low_background()", 0, gearman_client_do_low_background_basic },
  {0, 0, 0}
};

test_st gearman_client_do_job_handle_tests[] ={
  {"gearman_client_do_job_handle() no active tasks", 0, gearman_client_do_job_handle_no_active_task },
  {"gearman_client_do_job_handle() follow do command", 0, gearman_client_do_job_handle_follow_do },
  {0, 0, 0}
};

test_st gearman_execute_map_reduce_tests[] ={
  {"gearman_execute() map reduce", 0, gearman_execute_map_reduce_basic },
  {"gearman_execute(GEARMAN_ARGUMENT_TOO_LARGE) map reduce", 0, gearman_execute_map_reduce_check_parameters },
  {"gearman_execute(GEARMAN_WORK_FAIL) map reduce", 0, gearman_execute_map_reduce_workfail },
  {"gearman_execute() fail in reduction", 0, gearman_execute_map_reduce_fail_in_reduction },
  {"gearman_execute() with mapper function", 0, gearman_execute_map_reduce_use_as_function },
  {0, 0, 0}
};

test_st gearman_client_set_server_option_tests[] ={
  {"gearman_client_set_server_option(exceptions)", 0, gearman_client_set_server_option_exception},
  {"gearman_client_set_server_option(bad)", 0, gearman_client_set_server_option_bad},
  {0, 0, 0}
};

test_st gearman_task_tests[] ={
  {"gearman_client_add_task() ", 0, gearman_client_add_task_test},
  {"gearman_client_add_task() fail", 0, gearman_client_add_task_test_fail},
  {"gearman_client_add_task() bad workload", 0, gearman_client_add_task_test_bad_workload},
  {"gearman_client_add_task_background()", 0, gearman_client_add_task_background_test},
  {"gearman_client_add_task_low_background()", 0, gearman_client_add_task_low_background_test},
  {"gearman_client_add_task_high_background()", 0, gearman_client_add_task_high_background_test},
  {"gearman_client_add_task() exception", 0, gearman_client_add_task_exception},
  {"gearman_client_add_task() warning", 0, gearman_client_add_task_warning},
  {"gearman_client_add_task(GEARMAN_NO_SERVERS)", 0, gearman_client_add_task_no_servers},
  {0, 0, 0}
};


collection_st collection[] ={
  {"gearman_client_st", 0, 0, tests},
  {"gearman_client_st chunky", pre_chunk, post_function_reset, tests}, // Test with a worker that will respond in part
  {"gearman_strerror", 0, 0, gearman_strerror_tests},
  {"gearman_task", 0, 0, gearman_task_tests},
  {"gearman_task chunky", pre_chunk, post_function_reset, gearman_task_tests},
  {"gearman_task namespace", pre_namespace, post_function_reset, gearman_task_tests},
  {"unique", pre_unique, post_function_reset, unique_tests},
  {"gearman_client_do()", 0, 0, gearman_client_do_tests},
  {"gearman_client_do() namespace", pre_namespace, post_function_reset, gearman_client_do_tests},
  {"gearman_execute chunky", pre_chunk, post_function_reset, gearman_execute_tests},
  {"gearman_client_do_job_handle", 0, 0, gearman_client_do_job_handle_tests},
  {"gearman_client_do_job_handle namespace", pre_namespace, post_function_reset, gearman_client_do_job_handle_tests},
  {"gearman_client_do_background", 0, 0, gearman_client_do_background_tests},
  {"gearman_client_set_server_option", 0, 0, gearman_client_set_server_option_tests},
  {"gearman_execute", 0, 0, gearman_execute_tests},
  {"gearman_execute_map_reduce()", 0, 0, gearman_execute_map_reduce_tests},
  {"gearman_command_t", 0, 0, gearman_command_t_tests},
  {"regression_tests", 0, 0, regression_tests},
  {"client-logging", pre_logging, post_logging, tests_log},
  {0, 0, 0, 0}
};

typedef test_return_t (*libgearman_test_prepost_callback_fn)(client_test_st *);
typedef test_return_t (*libgearman_test_callback_fn)(gearman_client_st *);
static test_return_t _runner_prepost_default(libgearman_test_prepost_callback_fn func, client_test_st *container)
{
  if (func)
  {
    return func(container);
  }

  return TEST_SUCCESS;
}

static test_return_t _runner_default(libgearman_test_callback_fn func, client_test_st *container)
{
  if (func)
  {
    test_return_t rc;

    if (container->clone())
    {
      gearman_client_st *client= gearman_client_clone(NULL, container->client());
      test_truth(client);
      gearman_client_set_context(client, (void *)container->worker_name());
      rc= func(client);
      if (rc == TEST_SUCCESS)
        test_truth(not client->task_list);
      gearman_client_free(client);
    }
    else
    {
      gearman_client_set_context(container->client(), (void *)container->worker_name());
      rc= func(container->client());
      assert(not container->client()->task_list);
    }

    return rc;
  }

  return TEST_SUCCESS;
}

static world_runner_st runner= {
  (test_callback_runner_fn)_runner_prepost_default,
  (test_callback_runner_fn)_runner_default,
  (test_callback_runner_fn)_runner_prepost_default
};


void get_world(world_st *world)
{
  world->collections= collection;
  world->create= world_create;
  world->destroy= world_destroy;
  world->runner= &runner;
}
