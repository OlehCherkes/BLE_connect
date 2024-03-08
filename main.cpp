#include "BleSensor.h"
#include <iostream>
#include <thread>

int main(int argc, char* argv[])
{
  std::cout << "Noise Collector BLE" << std::endl;
  BleSensor bleSensor{ "ES1in000003" };
  bleSensor.scanAndConnect();
  while (1)
  {
    if (bleSensor.tempAttrProxy)
    {
      bleSensor.writeFromDolboeb();
      bleSensor.readTemperature();
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  //bleSensor.getValue();
  std::this_thread::sleep_for(std::chrono::seconds(10));
  bleSensor.disconnect();

  return 0;
}