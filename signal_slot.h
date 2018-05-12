/***********************************************
        File Name: signal_slot.h
        Author: Abby Cin
        Mail: abbytsing@gmail.com
        Created Time: 5/12/18 2:56 PM
***********************************************/

#ifndef SIGNAL_SLOT_H_
#define SIGNAL_SLOT_H_

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include <queue>
#include <logging.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace nm
{
  namespace meta
  {
    template<typename R> struct call_impl
    {
      template<typename F, typename Arg>
      static R call(F&& f, Arg& args)
      {
        R res;
        std::forward<F>(f)(args, &res);
        return res;
      }

      template<typename F, typename TupleType, size_t... Is>
      static void invoke(R* res, F&& f, TupleType& args, std::index_sequence<Is...>)
      {
        *res = std::forward<F>(f)(std::forward<std::tuple_element_t<Is, std::remove_reference_t<TupleType>>>(std::get<Is>(args))...);
      }

      template<typename Obj, typename F, typename TupleType, size_t... Is>
      static void invoke(R* res, Obj* obj, F&& f, TupleType& args, std::index_sequence<Is...>)
      {
        *res = std::forward<F>((obj)->*f)(std::forward<std::tuple_element_t<Is, std::remove_reference_t<TupleType>>>(std::get<Is>(args))...);
      }
    };
    template<> struct call_impl<void>
    {
      template<typename F, typename Arg>
      static void call(F&& f, Arg& args)
      {
        std::forward<F>(f)(args, nullptr);
      }

      template<typename F, typename TupleType, size_t... Is>
      static void invoke(void*, F&& f, TupleType& args, std::index_sequence<Is...>)
      {
        std::forward<F>(f)(std::forward<std::tuple_element_t<Is, std::remove_reference_t<TupleType>>>(std::get<Is>(args))...);
      }

      template<typename Obj, typename F, typename TupleType, size_t... Is>
      static void invoke(void*, Obj* obj, F f, TupleType& args, std::index_sequence<Is...>)
      {
        (obj->*f)(std::forward<std::tuple_element_t<Is, std::remove_reference_t<TupleType>>>(std::get<Is>(args))...);
      }
    };

    class FunctorMap
    {
      public:
        template<typename> struct Inspector;
        template<typename R, typename... Args>
        struct Inspector<R(Args...)>
        {
          using arg_t = std::tuple<std::remove_reference_t<Args>...>;
          using res_t = R;
        };
        template<typename R, typename... Args>
        struct Inspector<R(*)(Args...)> : Inspector<R(Args...)> {};
        template<typename R, typename... Args>
        struct Inspector<R(&)(Args...)> : Inspector<R(Args...)> {};
        template<typename R, typename Object, typename... Args>
        struct Inspector<R(Object::*)(Args...)> : Inspector<R(Args...)> {};
        template<typename R, typename Object, typename... Args>
        struct Inspector<R(Object::*)(Args...) const> : Inspector<R(Args...)> {};
        template<typename R, typename Object, typename... Args>
        struct Inspector<R(Object::*)(Args...) volatile> : Inspector<R(Args...)> {};
        template<typename R, typename Object, typename... Args>
        struct Inspector<R(Object::*)(Args...) const volatile> : Inspector<R(Args...)> {};

        // functor like
        template<typename Lambda>
        struct Inspector : Inspector<decltype(&Lambda::operator())> {};
        template<typename Lambda>
        struct Inspector<Lambda&> : Inspector<decltype(&Lambda::operator())> {};
        template<typename Lambda>
        struct Inspector<Lambda&&> : Inspector<Lambda&> {};

      public:
        using Fp = std::function<void(void*, void*)>;

        FunctorMap() = default;

        template<typename F>
        void bind(const std::string& sig, F f)
        {
          auto fp = [f = std::forward<F>(f)](void* args, void* res) {
            using arg_t = typename Inspector<F>::arg_t;
            using res_t = typename Inspector<F>::res_t;
            using indices = std::make_index_sequence<std::tuple_size<arg_t>::value>;
            arg_t& arg = *static_cast<arg_t*>(args);
            res_t* tmp = static_cast<res_t*>(res);
            call_impl<res_t>::invoke(tmp, f, arg, indices{});
          };
          if(functors_.find(sig) == functors_.end())
          {
            std::vector<Fp> v{std::move(fp)};
            functors_.insert({sig, std::move(v)});
          }
          else
          {
            functors_[sig].emplace_back(std::move(fp));
          }
        }

        template<typename Obj, typename F>
        void bind(const std::string &sig, Obj* obj, F f)
        {
          auto fp = [obj, f = std::forward<F>(f)](void* args, void* res) {
            using arg_t = typename Inspector<F>::arg_t;
            using res_t = typename Inspector<F>::res_t;
            using indices = std::make_index_sequence<std::tuple_size<arg_t>::value>;
            arg_t& arg = *static_cast<arg_t*>(args);
            res_t* tmp = static_cast<res_t*>(res);
            call_impl<res_t>::invoke(tmp, obj, f, arg, indices{});
            // destruct is necessary
            log_info() << "destruct";
            static_cast<arg_t*>(args)->~arg_t();
          };;
          if(functors_.find(sig) == functors_.end())
          {
            std::vector<Fp> v{std::move(fp)};
            functors_.insert({sig, std::move(v)});
          }
          else
          {
            functors_[sig].emplace_back(std::move(fp));
          }
        }

        template<typename R, typename... Args>
        R call(const std::string& sig, Args&&... args)
        {
          auto params = std::make_tuple(std::forward<Args>(args)...);
          if(functors_.find(sig) == functors_.end())
          {
            return R();
          }
          auto& fps = functors_[sig];
          for(auto& fp: fps)
          {
            return call_impl<R>::call(fp, params);
          }
          return R();
        }

        void notify(const std::string &sig, void* param)
        {
          if(functors_.find(sig) == functors_.end())
          {
            return;
          }
          auto& fps = functors_[sig];
          for(auto& fp: fps)
          {
            call_impl<void>::call(fp, param);
          }
        }

      private:
        std::map<std::string, std::vector<Fp>> functors_;
    };
  }
}

namespace nm
{
  class EventLoop
  {
    public:
      EventLoop()
      {
        ::socketpair(AF_UNIX, SOCK_STREAM, 0, pair_);
        efd_ = ::epoll_create(233);
        if(efd_ == -1)
        {
          log_err() << "epoll_create: " << strerror(errno);
        }
        struct epoll_event ev{};
        ev.data.fd = pair_[1]; // read side
        ev.events = EPOLLIN;

        if(::epoll_ctl(efd_, EPOLL_CTL_ADD, pair_[1], &ev) == -1)
        {
          log_err() << "epoll_ctl: " << strerror(errno);
        }
      }
      ~EventLoop()
      {
        ::close(efd_);
      }

      template<typename SenderObj, typename SenderFp, typename RecevierObj, typename ReceiverFp>
      void post(SenderObj* s_obj, SenderFp&& s_fp, RecevierObj* obj, ReceiverFp&& fp)
      {
        // cast pointer to member function to void* is unspecified behavior
        // g++ will show warnings, clang will show errors. but it's ok to do this
        // since we only want to get a unique number from it.
        std::string name = this->get_signature(s_obj, (void*)s_fp);
        fm_.bind(name, obj, fp);
      }

      template<typename Obj, typename F, typename... Args>
      void notify(Obj* obj, F&& f, Args&&... args)
      {
        Data_t data{};
        data.sig_ = this->get_signature(obj, (void*)f);
        using param_t = std::tuple<std::remove_reference_t<Args>...>;
        int arg_size = sizeof(param_t);
        //auto param = std::make_tuple(std::forward<Args>(args)...);
        //memcpy(data.param_, (void*)&param, arg_size);
        data.param_size_ = arg_size;
        new (data.param_) param_t{args...};
        queue_.push(std::move(data));
      }

      void exec()
      {
        while(running_)
        {
          int n = epoll_wait(efd_, rev_, sizeof(rev_), 1);
          if(n == -1)
          {
            log_err() << "epoll_wait: " << strerror(errno);
            if(errno != EINVAL)
            {
              return;
            }
          }
          if(!running_)
          {
            return;
          }
          // timeout

          while(!queue_.empty())
          {
            Data_t data{queue_.front()};
            queue_.pop();
            fm_.notify(data.sig_, data.param_);
          }
        }
      }

      void quit()
      {
        write(pair_[0], "0", 1);
        running_ = false;
      }

    private:
      struct Data_t
      {
        Data_t()
        {
          param_ = new char[1024];
        }

        ~Data_t()
        {
          if(param_ != nullptr)
          {
            delete[] param_;
          }
        }

        Data_t(Data_t&& rhs)
          : sig_{rhs.sig_},
            param_size_{rhs.param_size_},
            param_{rhs.param_}
        {
          rhs.param_ = nullptr;
        }

        Data_t(Data_t& rhs)
          : Data_t{std::move(rhs)}
        {};

        Data_t(const Data_t&) = delete;
        Data_t& operator= (const Data_t&) = delete;
        Data_t& operator= (Data_t&&) = delete;
        Data_t& operator= (Data_t&) = delete;

        std::string sig_{};
        int param_size_{0};
        char* param_{nullptr};
      };

      int efd_{-1};
      int pair_[2]{-1};
      bool running_{true};
      struct epoll_event rev_[1]{};
      meta::FunctorMap fm_{};
      std::queue<Data_t> queue_{};

      std::string get_signature(void* obj, void* fp)
      {
        return std::to_string(reinterpret_cast<uintptr_t>(obj))
               + std::to_string(reinterpret_cast<uintptr_t>(fp));
      }
  };

  class App;

  class Object
  {
    public:
      template<typename Obj, typename F, typename... Args>
      void notify(Obj* obj, F&& f, Args&&... args)
      {
        if(ev_ == nullptr)
        {
          return;
        }
        ev_->notify(obj, std::forward<F>(f), std::forward<Args>(args)...);
      }

      template<typename sObj, typename sF, typename rObj, typename rF>
      void post(sObj* sobj, sF&& sf, rObj* robj, rF&& rf)
      {
        if(ev_ == nullptr)
        {
          return;
        }
        ev_->post(sobj, sf, robj, rf);
      }

    private:
      friend App;
      inline static EventLoop* ev_{nullptr};
  };

  class App : public Object
  {
    public:
      App()
      {
        Object::ev_ = new EventLoop{};
      }

      ~App()
      {
        if(Object::ev_ != nullptr)
        {
          delete Object::ev_;
        }
      }

      void exec()
      {
        if(ev_ != nullptr)
        {
          ev_->exec();
        }
      }

      void quit()
      {
        if(Object::ev_ != nullptr)
        {
          ev_->quit();
        }
      }
  };
}
#endif //SIGNAL_SLOT_H_
