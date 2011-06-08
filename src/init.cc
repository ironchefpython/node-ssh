/**
 * @package node-ssh
 * @copyright  Copyright(c) 2011 Ajax.org B.V. <info AT ajax.org>
 * @author Gabor Krizsanits <gabor AT ajax DOT org>
 * @license http://github.com/ajaxorg/node-ssh/blob/master/LICENSE MIT License
 */

#include "sftp.h"
#include <libssh/callbacks.h>

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

// Thread callbacks are based on the libssh pthreads.c file

extern "C" {
static int ssh_pthread_mutex_init (void **priv)
{
  int err = 0;
  *priv = malloc (sizeof (pthread_mutex_t));
  if (*priv==NULL)
    return ENOMEM;
  err = pthread_mutex_init ((pthread_mutex_t*)*priv, NULL);
  if (err != 0){
    free (*priv);
    *priv=NULL;
  }
  return err;
}

static int ssh_pthread_mutex_destroy (void **lock) 
{
  int err = pthread_mutex_destroy ((pthread_mutex_t*)*lock);
  free (*lock);
  *lock=NULL;
  return err;
}

static int ssh_pthread_mutex_lock (void **lock) 
{
  return pthread_mutex_lock ((pthread_mutex_t*)*lock);
}

static int ssh_pthread_mutex_unlock (void **lock)
{
  return pthread_mutex_unlock ((pthread_mutex_t*)*lock);
}

static unsigned long ssh_pthread_thread_id (void)
{
#if _WIN32
    return (unsigned long) pthread_self().p;
#else
    return (unsigned long) pthread_self();
#endif
}

static struct ssh_threads_callbacks_struct ssh_threads_pthread =
{
  	"threads_pthread",
    ssh_pthread_mutex_init,
    ssh_pthread_mutex_destroy,
    ssh_pthread_mutex_lock,
    ssh_pthread_mutex_unlock,
    ssh_pthread_thread_id
};

struct ssh_threads_callbacks_struct *ssh_threads_get_pthread()
{
	return &ssh_threads_pthread;
}

void init(Handle<Object> target) 
{
    ssh_threads_set_callbacks(&ssh_threads_pthread);
    ssh_init();
    
    HandleScope scope;
    SFTP::Initialize(target);
}
} //extern "C"
