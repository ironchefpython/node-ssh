/**
 * @package node-ssh
 * @copyright  Copyright(c) 2011 Ajax.org B.V. <info AT ajax.org>
 * @author Gabor Krizsanits <gabor AT ajax DOT org>
 * @license http://github.com/ajaxorg/node-ssh/blob/master/LICENSE MIT License
 */
 
#include "sshBase.h"
#include <node_buffer.h>
#include <string.h>

/***********************************************
*                 SSHBase ß class
************************************************/

Persistent<String> SSHBase::callback_symbol;

SSHBase::SSHBase(const Arguments &) 
  : m_ssh_session(NULL)
  , m_error(NULL)
  , m_pub_key(NULL)
  , m_prv_key(NULL)
  , m_wdata(NULL)  
{
  m_error = (char*) malloc (SSH_MAX_ERROR);
}

SSHBase::~SSHBase()
{  
  freeSessions();
  resetData();
  if (m_pub_key)
    ssh_string_free(m_pub_key);
  if (m_prv_key)
    privatekey_free(m_prv_key);
  if (m_wdata)
   free(m_wdata);
  m_wdata = NULL;    
}

void setCharData(char*& to, Local<Value> data)
{
  String::Utf8Value str(Local<Object>::Cast(data));
  to = strdup(*str);
}

void SSHBase::resetData()
{
  if (m_error) 
    m_error[0] = (char) 0;
}

void SSHBase::freeSessions()
{
  if (m_ssh_session) {
    ssh_disconnect(m_ssh_session);
    ssh_free(m_ssh_session);
    m_ssh_session = NULL;
  }
}

int SSHBase::startConnect(eio_req *req)
{
  SSHBase* pthis = (SSHBase*) req->data;
  if (ssh_is_connected(pthis->m_ssh_session)) {
     ssh_disconnect(pthis->m_ssh_session);   
  }
  int res = ssh_connect(pthis->m_ssh_session);
  if (res != SSH_OK) {
    //fprintf(stderr, "connection failed\n");
    snprintf(pthis->m_error, SSH_MAX_ERROR, 
            "Error connecting: %s\n",
            ssh_get_error(pthis->m_ssh_session));
    return -1;
  }
  if (pthis->m_prv_key && pthis->m_pub_key) {
    res = ssh_userauth_pubkey (pthis->m_ssh_session,
      NULL, pthis->m_pub_key, pthis->m_prv_key);	
    
    if (res != SSH_AUTH_SUCCESS)  {
      snprintf(pthis->m_error, SSH_MAX_ERROR, 
        "Error authenticating with keys: %s\n",
        ssh_get_error(pthis->m_ssh_session));
      return -1;        
    }  
  }  
  else {
    res = ssh_userauth_autopubkey(pthis->m_ssh_session, NULL);    
    if (res != SSH_AUTH_SUCCESS)  {
      snprintf(pthis->m_error, SSH_MAX_ERROR, 
        "Error authenticating with password: %s\n",
        ssh_get_error(pthis->m_ssh_session));
      return -1;        
    }
  }
  return 1;
}

int SSHBase::startSetPubKey(eio_req *req)
{
  SSHBase* pthis = (SSHBase*) req->data;

  pthis->m_pub_key = publickey_from_file(pthis->m_ssh_session, 
    pthis->m_wdata, NULL);    
  if (!pthis->m_pub_key) {
    snprintf(pthis->m_error, SSH_MAX_ERROR, "Can't set public key.");
  }
  return 0;
}

int SSHBase::startSetPrvKey(eio_req *req)
{
  SSHBase* pthis = (SSHBase*) req->data;
  
  pthis->m_prv_key = privatekey_from_file(pthis->m_ssh_session, 
    pthis->m_wdata, 0, NULL);		
  if (!pthis->m_prv_key) {
    snprintf(pthis->m_error, SSH_MAX_ERROR, "Can't set private key.");
  }
  return 0;
}

int SSHBase::onDone(eio_req *req)
{
  SSHBase* pthis = (SSHBase*) req->data;
  HandleScope scope;
  Handle<Value> argv[1];
  argv[0] = String::New(pthis->m_error);
  pthis->Emit(callback_symbol, 1, argv);
  ev_unref(EV_DEFAULT_UC);
  return 0;
}

void setOption(ssh_session& session, Local<Object>& obj, 
                      const char* prop_name, ssh_options_e opt) 
{
  Local<Object> prop = Local<Object>::Cast(
      obj->Get(String::NewSymbol(prop_name))
  );
  if (!prop->IsUndefined()) {
      Local<String> value = prop->ToString();
      char *cvalue = new char[ value->Length() + 1 ];
      value->WriteAscii(cvalue); 
      ssh_options_set(session, opt, cvalue);
      delete cvalue;
  }
}

void setMember(char*& member, Local<Object>& obj, 
                      const char* prop_name) 
{
  Local<Object> prop = Local<Object>::Cast(
      obj->Get(String::NewSymbol(prop_name))
  );
  if (!prop->IsUndefined()) {
      Local<String> value = prop->ToString();
      member = new char[ value->Length() + 1 ];
      value->WriteAscii(member); 
  }
}

void setMember(int& member, Local<Object>& obj, 
                      const char* prop_name) 
{
  Local<Object> prop = Local<Object>::Cast(
      obj->Get(String::NewSymbol(prop_name))
  );
  if (!prop->IsUndefined()) {
      member = prop->Int32Value();
  }
}

Handle<Value> SSHBase::init(const Arguments &args) 
{  
  HandleScope scope;
  SSHBase *pthis = ObjectWrap::Unwrap<SSHBase>(args.This());
  pthis->freeSessions();
  
  pthis->m_ssh_session = ssh_new();
  if (!pthis->m_ssh_session)
    return False();
  
  Local<Object> opt = Local<Object>::Cast(args[0]);    
  setOption(pthis->m_ssh_session, opt, "host", SSH_OPTIONS_HOST);
  setOption(pthis->m_ssh_session, opt, "port", SSH_OPTIONS_PORT_STR);
  setOption(pthis->m_ssh_session, opt, "user", SSH_OPTIONS_USER);
  long timeout = 10;
  ssh_options_set(pthis->m_ssh_session, SSH_OPTIONS_TIMEOUT, (void*) &timeout);
  //ssh_options_set(pthis->m_ssh_session, SSH_OPTIONS_LOG_VERBOSITY, (void*) new int(SSH_LOG_PACKET));
  return True();  
}

Handle<Value> SSHBase::connect(const Arguments &args) 
{
  HandleScope scope;
  SSHBase *pthis = ObjectWrap::Unwrap<SSHBase>(args.This());
  pthis->resetData();
  // TODO: key validation
  //String::Utf8Value password(args[0]->ToString());
  //String::Utf8Value pub_key(args[1]->ToString());
  
  eio_custom(startConnect, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC);
    
  return True();
}

Handle<Value> SSHBase::setPubKey(const Arguments &args){
  HandleScope scope;
  SSHBase *pthis = ObjectWrap::Unwrap<SSHBase>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_wdata, args[0]);
  eio_custom(startSetPubKey, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value> SSHBase::setPrvKey(const Arguments &args){
  HandleScope scope;
  SSHBase *pthis = ObjectWrap::Unwrap<SSHBase>(args.This()); 
  pthis->resetData();
  setCharData(pthis->m_wdata, args[0]);
  eio_custom(startSetPrvKey, EIO_PRI_DEFAULT, onDone, pthis);
  ev_ref(EV_DEFAULT_UC); 
  return True();
}

Handle<Value> SSHBase::interrupt(const Arguments &args){
  HandleScope scope;
  SSHBase *pthis = ObjectWrap::Unwrap<SSHBase>(args.This()); 
  //fprintf(stderr, "interrupt\n");
  //ssh_disconnect(pthis->m_ssh_session);
  //ssh_silent_disconnect(pthis->m_ssh_session);
  ssh_set_fd_except(pthis->m_ssh_session);
  pthis->freeSessions();
  fprintf(stderr, "interrupted\n");
  return True();
}
