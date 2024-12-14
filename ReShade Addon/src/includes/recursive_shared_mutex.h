// Shared Mutex with support for multiple exclusive/unique locks from the same thread.
// Multiple shared locks on the same thread is already allowed to begin with (hopefully, theoretically the behaviour is undefined).
// First doing a shared lock and then a unique lock on the same thread, or the opposite, causes a deadlock, and there's no proper way of upgrading a lock from shared to unique.
class recursive_shared_mutex : public std::shared_mutex
{
public:
   void lock()
   {
      std::thread::id this_id = std::this_thread::get_id();
      if (owner == this_id)
      {
         // recursive locking
         count++;
      }
      else
      {
         // normal locking
         std::shared_mutex::lock();
         owner = this_id;
         count = 1;
      }
   }
   void unlock()
   {
      if (count > 1)
      {
         // recursive unlocking
         count--;
      }
      else
      {
         // normal unlocking
         owner = std::thread::id();
         count = 0;
         std::shared_mutex::unlock();
      }
   }

private:
   std::atomic<std::thread::id> owner;
   int count;
};