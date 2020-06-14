#include <iostream>
#include <fstream>
#include "timer_service.h"

using namespace std;

void myfunc(int a) {
    // open a file in write mode.
   //ofstream outfile;
   //outfile.open("a.txt");
   // again write inputted data into the file.
   //outfile << a << endl;
   //outfile << "added" << endl;
   cout<<a<<endl;

   // close the opened file.
   //outfile.close();
}

int main() {
    timer_service::timer_.schedule(1000,false,myfunc,1);
    timer_service::timer_.schedule(500,false,myfunc,2);
    //std::this_thread::sleep_for(chrono::seconds(2));
    timer_service::timer_.join();
    //timer_service::timer* my_timer = new timer_service::timer();
    //my_timer->schedule(1000,false,myfunc,1);
}