/*!
 *  Copyright (c) 2016 by Contributors
 * \file c_api_executor.cc
 * \brief C API of mxnet
 */
#include <mxnet/base.h>
#include <mxnet/c_api.h>
#include <mxnet/executor.h>
#include "./c_api_common.h"

int MXExecutorPrint(ExecutorHandle handle, const char **out_str) {
  Executor *exec = static_cast<Executor*>(handle);
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  std::ostringstream os;
  exec->Print(os);
  ret->ret_str = os.str();
  *out_str = (ret->ret_str).c_str();
  API_END();
}

int MXExecutorFree(ExecutorHandle handle) {
  API_BEGIN();
  delete static_cast<Executor*>(handle);
  API_END();
}

int MXExecutorForward(ExecutorHandle handle, int is_train) {
  API_BEGIN();
  Executor *exec = static_cast<Executor*>(handle);
  exec->Forward(is_train != 0);
  API_END();
}

int MXExecutorBackward(ExecutorHandle handle,
                       mx_uint len,
                       NDArrayHandle *head_grads) {
  API_BEGIN();
  Executor *exec = static_cast<Executor*>(handle);
  std::vector<NDArray> ndarrays;
  NDArray **args_ptr = reinterpret_cast<NDArray**>(head_grads);
  for (mx_uint i = 0; i < len; ++i) {
    ndarrays.push_back(*args_ptr[i]);
  }
  exec->Backward(ndarrays);
  API_END();
}

int MXExecutorOutputs(ExecutorHandle handle,
                      mx_uint *out_size,
                      NDArrayHandle **out) {
  MXAPIThreadLocalEntry *ret = MXAPIThreadLocalStore::Get();
  API_BEGIN();
  Executor *exec = static_cast<Executor*>(handle);
  std::vector<NDArray> heads = exec->outputs();
  ret->ret_handles.resize(heads.size());
  for (size_t i = 0; i < heads.size(); ++i) {
    NDArray *ptr = new NDArray();
    *ptr = heads[i];
    ret->ret_handles[i] = ptr;
  }
  *out_size = heads.size();
  *out = dmlc::BeginPtr(ret->ret_handles);
  API_END();
}

int MXExecutorBind(SymbolHandle symbol_handle,
                   int dev_type,
                   int dev_id,
                   mx_uint len,
                   NDArrayHandle *in_args,
                   NDArrayHandle *arg_grad_store,
                   mx_uint *grad_req_type,
                   mx_uint aux_states_len,
                   NDArrayHandle *aux_states,
                   ExecutorHandle *out) {
  return MXExecutorBindX(symbol_handle,
                         dev_type, dev_id,
                         0, nullptr, nullptr, nullptr,
                         len, in_args, arg_grad_store, grad_req_type,
                         aux_states_len, aux_states, out);
}

int MXExecutorBindX(SymbolHandle symbol_handle,
                    int dev_type,
                    int dev_id,
                    mx_uint num_map_keys,
                    const char** map_keys,
                    const int* map_dev_types,
                    const int* map_dev_ids,
                    mx_uint len,
                    NDArrayHandle *in_args,
                    NDArrayHandle *arg_grad_store,
                    mx_uint *grad_req_type,
                    mx_uint aux_states_len,
                    NDArrayHandle *aux_states,
                    ExecutorHandle *out) {
  return MXExecutorBindEX(symbol_handle,
                          dev_type, dev_id,
                          num_map_keys, map_keys, map_dev_types, map_dev_ids,
                          len, in_args, arg_grad_store, grad_req_type,
                          aux_states_len, aux_states,
                          NULL, out);
}

int MXExecutorSimpleBind(SymbolHandle symbol_handle,
                         int dev_type,  // default device type
                         int dev_id,  // default device id
                         mx_uint num_map_keys,  // num of keys in group2ctx
                         const char** map_keys,  // arg names of group2ctx
                         const int* map_dev_types,  // ctx dev_types of group2ctx
                         const int* map_dev_ids,  // ctx dev_ids of group2ctx
                         mx_uint in_arg_len,  // arg lengths, same for arg_grads
                         const int* in_arg_dev_types,  // in_arg dev_types
                         const int* in_arg_dev_ids,  // in_arg dev_ids
                         const int* arg_grad_dev_types,  // arg_grad dev_tyeps
                         const int* arg_grad_dev_ids,  // arg_grad dev_ids
                         const mx_uint* in_arg_shape_data,  // shape data of all arguments
                         const mx_uint* in_arg_shape_idx,  // shape idx of each argument
                         const int* in_arg_dtypes,  // dtypes of in_args
                         const mx_uint* grad_req_types,  // req types of args_grad
                         mx_uint aux_state_len,  // number of aux_states
                         const int* aux_state_dev_types,  // aux_state ctx dev_types
                         const int* aux_state_dev_ids,  // aux_state ctx dev_ids
                         const mx_uint* aux_state_shape_data,  // shape data of all aux_states
                         const mx_uint* aux_state_shape_idx,  // shape idx of each aux_state
                         const int* aux_state_dtypes,  // dtypes of aux_states
                         NDArrayHandle* in_args,
                         NDArrayHandle* arg_grads,
                         NDArrayHandle* aux_states,
                         ExecutorHandle *out) {
  Executor* exec = nullptr;

  API_BEGIN();
  nnvm::Symbol *symb = static_cast<nnvm::Symbol*>(symbol_handle);
  // create default ctx
  Context ctx = Context::Create(static_cast<Context::DeviceType>(dev_type), dev_id);

  // create ctx map
  std::map<std::string, Context> ctx_map;
  for (mx_uint i = 0; i < num_map_keys; ++i) {
    ctx_map[std::string(map_keys[i])] = Context::Create(
        static_cast<Context::DeviceType>(map_dev_types[i]), map_dev_ids[i]);
  }

  // create ctxes, dtypes, ndarray holders for in_args and arg_grads
  NDArray** in_arg_ptrs = reinterpret_cast<NDArray**>(in_args);
  NDArray** arg_grad_ptrs = reinterpret_cast<NDArray**>(arg_grads);
  std::vector<Context> in_arg_ctx_vec;
  std::vector<Context> arg_grad_ctx_vec;
  std::vector<TShape> in_arg_shape_vec;
  std::vector<int> in_arg_dtype_vec;
  std::vector<OpReqType> grad_req_type_vec;
  std::vector<NDArray*> in_arg_vec;
  std::vector<NDArray*> arg_grad_vec;
  for (mx_uint i = 0; i < in_arg_len; ++i) {
    in_arg_ctx_vec.push_back(Context::Create(
          static_cast<Context::DeviceType>(in_arg_dev_types[i]), in_arg_dev_ids[i]));
    in_arg_shape_vec.push_back(TShape(in_arg_shape_data+in_arg_shape_idx[i],
                                      in_arg_shape_data+in_arg_shape_idx[i+1]));
    in_arg_dtype_vec.push_back(in_arg_dtypes[i]);
    in_arg_vec.push_back(in_arg_ptrs[i]);
    if (arg_grad_ptrs[i] == nullptr) {
      arg_grad_ctx_vec.push_back(Context());
      arg_grad_vec.push_back(nullptr);
      grad_req_type_vec.push_back(kNullOp);
    } else {
      arg_grad_ctx_vec.push_back(Context::Create(
            static_cast<Context::DeviceType>(arg_grad_dev_types[i]), arg_grad_dev_ids[i]));
      arg_grad_vec.push_back(arg_grad_ptrs[i]);
      grad_req_type_vec.push_back(static_cast<OpReqType>(grad_req_types[i]));
    }
  }

  NDArray** aux_state_ptrs = reinterpret_cast<NDArray**>(aux_states);
  std::vector<Context> aux_state_ctx_vec;
  std::vector<TShape> aux_state_shape_vec;
  std::vector<int> aux_state_dtype_vec;
  std::vector<NDArray*> aux_state_vec;
  for (mx_uint i = 0; i < aux_state_len; ++i) {
    aux_state_ctx_vec.push_back(Context::Create(
          static_cast<Context::DeviceType>(aux_state_dev_types[i]), aux_state_dev_ids[i]));
    aux_state_shape_vec.push_back(TShape(aux_state_shape_data+aux_state_shape_idx[i],
                                         aux_state_shape_data+aux_state_shape_idx[i+1]));
    aux_state_dtype_vec.push_back(aux_state_dtypes[i]);
    aux_state_vec.push_back(aux_state_ptrs[i]);
  }

  *out = Executor::SimpleBind(*symb, ctx, ctx_map, in_arg_ctx_vec, arg_grad_ctx_vec,
                              aux_state_ctx_vec, in_arg_shape_vec, aux_state_shape_vec,
                              in_arg_dtype_vec, aux_state_dtype_vec, grad_req_type_vec,
                              &in_arg_vec, &arg_grad_vec, &aux_state_vec);

  // copy in_arg_vec, arg_grad_vec, and aux_state_vec back to
  // in_arg_ptrs, arg_grad_ptrs, and aux_state_ptrs
  std::copy(in_arg_vec.begin(), in_arg_vec.end(), in_arg_ptrs);
  std::copy(arg_grad_vec.begin(), arg_grad_vec.end(), arg_grad_ptrs);
  std::copy(aux_state_vec.begin(), aux_state_vec.end(), aux_state_ptrs);
  API_END_HANDLE_ERROR(delete exec);
}

int MXExecutorBindEX(SymbolHandle symbol_handle,
                     int dev_type,
                     int dev_id,
                     mx_uint num_map_keys,
                     const char** map_keys,
                     const int* map_dev_types,
                     const int* map_dev_ids,
                     mx_uint len,
                     NDArrayHandle *in_args,
                     NDArrayHandle *arg_grad_store,
                     mx_uint *grad_req_type,
                     mx_uint aux_states_len,
                     NDArrayHandle *aux_states,
                     ExecutorHandle shared_exec,
                     ExecutorHandle *out) {
  Executor* exec = nullptr;

  API_BEGIN();
  nnvm::Symbol *symb = static_cast<nnvm::Symbol*>(symbol_handle);
  Context ctx = Context::Create(static_cast<Context::DeviceType>(dev_type), dev_id);
  std::map<std::string, Context> ctx_map;
  for (mx_uint i = 0; i < num_map_keys; ++i) {
    ctx_map[std::string(map_keys[i])] = Context::Create(
        static_cast<Context::DeviceType>(map_dev_types[i]), map_dev_ids[i]);
  }
  NDArray **in_args_ptr = reinterpret_cast<NDArray**>(in_args);
  NDArray **arg_grad_ptr = reinterpret_cast<NDArray**>(arg_grad_store);
  NDArray **aux_states_ptr = reinterpret_cast<NDArray**>(aux_states);
  std::vector<NDArray> in_args_vec;
  std::vector<NDArray> arg_grad_vec;
  std::vector<OpReqType> grad_req_vec;
  std::vector<NDArray> aux_states_vec;
  for (mx_uint i = 0; i < len; ++i) {
    in_args_vec.push_back(*(in_args_ptr[i]));
    if (arg_grad_ptr[i] == nullptr) {
      arg_grad_vec.push_back(NDArray());
      grad_req_vec.push_back(kNullOp);
    } else {
      arg_grad_vec.push_back(*(arg_grad_ptr[i]));
      grad_req_vec.push_back(static_cast<OpReqType>(grad_req_type[i]));
    }
  }
  for (mx_uint i = 0; i < aux_states_len; ++i) {
    aux_states_vec.push_back(*(aux_states_ptr[i]));
  }
  *out = Executor::Bind(*symb, ctx, ctx_map, in_args_vec,
                        arg_grad_vec, grad_req_vec, aux_states_vec,
                        reinterpret_cast<Executor*>(shared_exec));
  API_END_HANDLE_ERROR(delete exec);
}

int MXExecutorSetMonitorCallback(ExecutorHandle handle,
                                 ExecutorMonitorCallback callback,
                                 void* callback_handle) {
  API_BEGIN();
  ExecutorMonitorCallback callback_temp = callback;
  void* callback_handle_temp = callback_handle;
  std::function<void(const char*, void*)> clbk
  = [callback_temp, callback_handle_temp](const char *name, void* handle) {
    callback_temp(name, handle, callback_handle_temp);
  };
  Executor *exec = static_cast<Executor*>(handle);
  exec->SetMonitorCallback(clbk);
  API_END();
}
