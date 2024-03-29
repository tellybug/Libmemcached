/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Libmemcached library
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/
 *  Copyright (C) 2010 Brian Aker All rights reserved.
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
#include <libmemcached/common.h>

const char * memcached_lib_version(void) 
{
  return LIBMEMCACHED_VERSION_STRING;
}

static inline memcached_return_t memcached_version_binary(memcached_st *ptr);
static inline memcached_return_t memcached_version_textual(memcached_st *ptr);

memcached_return_t memcached_version(memcached_st *ptr)
{
  if (ptr->flags.use_udp)
    return MEMCACHED_NOT_SUPPORTED;

  memcached_return_t rc;

  if (ptr->flags.binary_protocol)
    rc= memcached_version_binary(ptr);
  else
    rc= memcached_version_textual(ptr);      

  return rc;
}

static inline memcached_return_t memcached_version_textual(memcached_st *ptr)
{
  size_t send_length;
  memcached_return_t rc;
  char buffer[MEMCACHED_DEFAULT_COMMAND_SIZE];
  char *response_ptr;
  const char *command= "version\r\n";

  send_length= sizeof("version\r\n") -1;

  rc= MEMCACHED_SUCCESS;
  for (uint32_t x= 0; x < memcached_server_count(ptr); x++)
  {
    memcached_return_t rrc;
    memcached_server_write_instance_st instance=
      memcached_server_instance_fetch(ptr, x);

    // Optimization, we only fetch version once.
    if (instance->major_version != UINT8_MAX)
      continue;

    rrc= memcached_do(instance, command, send_length, true);
    if (rrc != MEMCACHED_SUCCESS)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    rrc= memcached_response(instance, buffer, MEMCACHED_DEFAULT_COMMAND_SIZE, NULL);
    if (rrc != MEMCACHED_SUCCESS)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    /* Find the space, and then move one past it to copy version */
    response_ptr= index(buffer, ' ');
    response_ptr++;

    instance->major_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    response_ptr= index(response_ptr, '.');
    response_ptr++;

    instance->minor_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    response_ptr= index(response_ptr, '.');
    response_ptr++;
    instance->micro_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }
  }

  return rc;
}

static inline memcached_return_t memcached_version_binary(memcached_st *ptr)
{
  memcached_return_t rc;
  protocol_binary_request_version request= {};
  request.message.header.request.magic= PROTOCOL_BINARY_REQ;
  request.message.header.request.opcode= PROTOCOL_BINARY_CMD_VERSION;
  request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;

  rc= MEMCACHED_SUCCESS;
  for (uint32_t x= 0; x < memcached_server_count(ptr); x++) 
  {
    memcached_return_t rrc;

    memcached_server_write_instance_st instance=
      memcached_server_instance_fetch(ptr, x);

    if (instance->major_version != UINT8_MAX)
      continue;

    rrc= memcached_do(instance, request.bytes, sizeof(request.bytes), true);
    if (rrc != MEMCACHED_SUCCESS) 
    {
      memcached_io_reset(instance);
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }
  }

  for (uint32_t x= 0; x < memcached_server_count(ptr); x++) 
  {
    memcached_server_write_instance_st instance=
      memcached_server_instance_fetch(ptr, x);

    if (instance->major_version != UINT8_MAX)
      continue;

    if (memcached_server_response_count(instance) > 0) 
    {
      memcached_return_t rrc;
      char buffer[32];
      char *p;

      rrc= memcached_response(instance, buffer, sizeof(buffer), NULL);
      if (rrc != MEMCACHED_SUCCESS) 
      {
        memcached_io_reset(instance);
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

      instance->major_version= (uint8_t)strtol(buffer, &p, 10);
      if (errno == ERANGE)
      {
        instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

      instance->minor_version= (uint8_t)strtol(p + 1, &p, 10);
      if (errno == ERANGE)
      {
        instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

      instance->micro_version= (uint8_t)strtol(p + 1, NULL, 10);
      if (errno == ERANGE)
      {
        instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

    }
  }

  return rc;
}
