//Used to temporarily store data to show at a later time/date
struct ScheduledMessage {
  String Message;
  long ScheduledDateTimeUnix;
  bool ShowIndefinitely;
};

//Used as a work around for issues that linked list has with other libraries
//Taken from here: https://github.com/vortigont/LinkedList/commit/39c2628a7794fc6e88e024d7fc82d52bc833c332
//Credit: vortigont
namespace LL { 
#include <LinkedList.h>
}

template<typename T> using LNode = LL::ListNode<T>;
template<typename T> using LList = LL::LinkedList<T>;

//Status reply from a unit, used by the telnet console
struct UnitStatus {
  bool responded;
  int status;       // 0 ready, 1 busy, 2 cal searching, 3 cal offset, 4 cal error
  int position;     // steps since HOME (calibration mode only)
  int version;      // 13 = 1.3.0, 255 = older firmware that doesn't report a version
};
