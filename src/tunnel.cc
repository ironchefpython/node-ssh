/**
 * @package node-ssh
 * @copyright  Copyright(c) 2011 Ajax.org B.V. <info AT ajax.org>
 * @author Gabor Krizsanits <gabor AT ajax DOT org>
 * @license http://github.com/ajaxorg/node-ssh/blob/master/LICENSE MIT License
 */
 
#include "tunnel.h"
#include <node_buffer.h>
#include <string.h>

Persistent<FunctionTemplate> Tunnel::constructor_template;


/***********************************************
*                 Tunnel class
************************************************/

Tunnel::Tunnel(const Arguments& args) 
  : SSHBase(args)
  , m_ssh_channel(NULL)
  , m_remote_host(NULL)
  , m_remote_port(NULL)
  , m_src_host(NULL)
  , m_local_port(NULL)
{
  m_rdata = (char*) malloc (SSH_READ_BUFFER_SIZE);
}

Tunnel::~Tunnel()
{  
  freeSessions();
  resetData();
  if (m_rdata)
    free(m_rdata);
}

Handle<Value> Tunnel::New(const Arguments &args) 
{  
  HandleScope scope;
  Tunnel *tunnel = new Tunnel(args);
  tunnel->Wrap(args.This());
  return args.This();
}

void Tunnel::resetData()
{
  SSHBase::resetData();
  m_size = m_pos = m_done = 0;
}

void Tunnel::freeSessions()
{ 
  if (m_ssh_channel) {
    ssh_channel_close(m_ssh_channel);
    ssh_channel_free(m_ssh_channel);
    m_ssh_channel = NULL;
  }
  SSHBase::freeSessions();
}

int Tunnel::startConnect(eio_req *req)
{
  Tunnel* pthis = (Tunnel*) req->data;
  if (SSHBase::startConnect(req)<0) {
    return 0;
  }
  pthis->m_ssh_channel = ssh_channel_new(pthis->m_ssh_session);
  int res = channel_open_forward(pthis->m_ssh_channel,
                            pthis->m_remote_host, pthis->m_remote_port,
                            pthis->m_src_host, pthis->m_local_port);
  if (res != SSH_OK) {
    snprintf(pthis->m_error, SSH_MAX_ERROR, 
        "Error opening a channel for port forwarding: %s\n",
        ssh_get_error(pthis->m_ssh_session));    
    ssh_channel_free(pthis->m_ssh_channel);    
  }
  return 0;
}

int Tunnel::startWrite(eio_req *req)
{
  Tunnel* pthis = (Tunnel*) req->data;
  int res = ssh_channel_write(pthis->m_ssh_channel,pthis->m_wdata,pthis->m_size);
  //fprintf(stderr, "written: %d\n", res);
  if (res < pthis->m_size) {
    snprintf(pthis->m_error, SSH_MAX_ERROR, 
        "Error writing channel: %s\n",
        ssh_get_error(pthis->m_ssh_session));    
  }
  return 0;
}

int Tunnel::startRead(eio_req *req)
{
  Tunnel* pthis = (Tunnel*) req->data;
  int available = ssh_channel_poll(pthis->m_ssh_channel, 0);
  if (available) {
    int res = ssh_channel_read(pthis->m_ssh_channel,
      pthis->m_rdata,SSH_MIN(available, SSH_READ_BUFFER_SIZE),0);
    if (res<0) {
        snprintf(pthis->m_error, SSH_MAX_ERROR, 
          "Error reading channel: %s\n",
          ssh_get_error(pthis->m_ssh_session));
      pthis->m_size = 0;
    }
    else {
      pthis->m_size = res;
    }
  }  
  return 0;
}

int Tunnel::onRead(eio_req *req)
{
  Tunnel* pthis = (Tunnel*) req->data;
  HandleScope scope;
  Handle<Value> argv[2];
  argv[0] = String::New(pthis->m_error);
  if (pthis->m_size)
    argv[1] = createBuffer(pthis->m_rdata, pthis->m_size);
  pthis->Emit(callback_symbol, pthis->m_size ? 2 : 1, argv);
  ev_unref(EV_DEFAULT_UC);
  return 0;
}

void Tunnel::Initialize(Handle<Object>& target)
{
    HandleScope scope;
    static int inited = 0;
    if (!inited) {
      Local<FunctionTemplate> t = FunctionTemplate::New(New);
      constructor_template = Persistent<FunctionTemplate>::New(t);
      constructor_template->Inherit(EventEmitter::constructor_template);
      constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
      constructor_template->SetClassName(String::NewSymbol("Tunnel"));
      
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "init", init);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "connect", connect);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "setPubKey", SSHBase::setPubKey);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "setPrvKey", SSHBase::setPrvKey);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "interrupt", SSHBase::interrupt);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "read", read);
      NODE_SET_PROTOTYPE_METHOD(constructor_template, "write", write);
      callback_symbol = NODE_PSYMBOL("callback");
      inited = true;
    }
    
    target->Set(
        String::NewSymbol("Tunnel"),
        constructor_template->GetFunction()
    );
}

Handle<Value> Tunnel::init(const Arguments &args) 
{  
  HandleScope scope;
  Tunnel *pthis = ObjectWrap::Unwrap<Tunnel>(args.This());
  pthis->freeSessions();
  
  pthis->m_ssh_session = ssh_new();
  if (!pthis->m_ssh_session)
    return False();
  
  Local<Object> opt = Local<Object>::Cast(args[0]);    
  setOption(pthis->m_ssh_session, opt, "host", SSH_OPTIONS_HOST);
  setOption(pthis->m_ssh_session, opt, "port", SSH_OPTIONS_PORT_STR);
  setOption(pthis->m_ssh_session, opt, "user", SSH_OPTIONS_USER);
  setMember(pthis->m_remote_host, opt, "remoteHost");  
  setMember(pthis->m_remote_port, opt, "remotePort");  
  setMember(pthis->m_src_host, opt, "srcHost");
  setMember(pthis->m_local_port, opt, "localPort");  

  long timeout = 10;
  ssh_options_set(pthis->m_ssh_session, SSH_OPTIONS_TIMEOUT, (void*) &timeout);
  //ssh_options_set(pthis->m_ssh_session, SSH_OPTIONS_LOG_VERBOSITY, (void*) new int(SSH_LOG_PACKET));
  return True();  
}

Handle<Value> Tunnel::connect(const Arguments &args) 
{
  HandleScope scope;
  Tunnel *pthis = ObjectWrap::Unwrap<Tunnel>(args.This());
  pthis->resetData();
  // TODO: key validation
  //String::Utf8Value password(args[0]->ToString());
  //String::Utf8Value pub_key(args[1]->ToString());
  
  eio_custom(startConnect, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC);
    
  return True();
}

Handle<Value> Tunnel::write(const Arguments &args) 
{
  HandleScope scope;
  Tunnel *pthis = ObjectWrap::Unwrap<Tunnel>(args.This()); 
  pthis->resetData();
  Local<Object> buffer_obj = args[0]->ToObject();
  pthis->m_size = Buffer::Length(buffer_obj);
  pthis->m_wdata = (char*) malloc (pthis->m_size);
  memcpy(pthis->m_wdata, Buffer::Data(buffer_obj), pthis->m_size);
  eio_custom(startWrite, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value> Tunnel::read(const Arguments &args) 
{
  HandleScope scope;
  Tunnel *pthis = ObjectWrap::Unwrap<Tunnel>(args.This());  
  pthis->resetData();
  eio_custom(startRead, EIO_PRI_DEFAULT, onRead, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}
