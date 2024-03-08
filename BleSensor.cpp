#include "BleSensor.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <regex>
#include <cmath>

BleSensor::BleSensor(const std::string& sensor_name)
  : deviceProxy{ nullptr }, tempAttrProxy{ nullptr },
  cv{}, mtx{}, connected{ false }, deviceName{ sensor_name }
{
  const std::string OBJECT_PATH{ "/org/bluez/hci0" };

  bluezProxy = sdbus::createProxy(SERVICE_BLUEZ, OBJECT_PATH);
  rootProxy = sdbus::createProxy(SERVICE_BLUEZ, "/");
}

void BleSensor::scanAndConnect()
{
  std::cout << "(TID: " << std::this_thread::get_id() << ") ";
  try
  {
    // Enable Bluetooth if not yet enabled
    if (getBluetoothStatus())
    {
      std::cout << "Bluetooth powered ON\n";
    }
    else
    {
      std::cout << "Powering bluetooth ON\n";
      setBluetoothStatus(true);
    }
    std::unique_lock<std::mutex> lock(mtx);
    subscribeToInterfacesAdded();
    enableScanning(true);
    // Wait to be connected to the sensor
    cv.wait(lock, [this]()
      { return connected; });
    enableScanning(false);
  }
  catch (sdbus::Error& error)
  {
    std::cerr << "ERR: on scanAndConnect(): " << error.getName() << " with message " << error.getMessage() << std::endl;
  }
}

void BleSensor::getValue()
{
  readTemperature();
}

void BleSensor::disconnect()
{
  std::cout << "Disconnecting from device";
  disconnectFromDevice();
}

bool BleSensor::getBluetoothStatus()
{
  const std::string METHOD_GET{ "Get" };
  sdbus::Variant variant;
  // Invoke a method that gets a property as a variant
  bluezProxy->callMethod(METHOD_GET)
    .onInterface(INTERFACE_PROPERTIES)
    .withArguments(INTERFACE_ADAPTER, PROPERTY_POWERED)
    .storeResultsTo(variant);
  return (bool)variant;
}

void BleSensor::setBluetoothStatus(bool enable)
{
  const std::string METHOD_SET{ "Set" };
  // Invoke a method that sets a property as a variant
  bluezProxy->callMethod(METHOD_SET)
    .onInterface(INTERFACE_PROPERTIES)
    .withArguments(INTERFACE_ADAPTER, PROPERTY_POWERED, sdbus::Variant(enable))
    // .dontExpectReply();
    .storeResultsTo();
}

void BleSensor::enableScanning(bool enable)
{
  const std::string METHOD_START_DISCOVERY{ "StartDiscovery" };
  const std::string METHOD_STOP_DISCOVERY{ "StopDiscovery" };
  std::cout << (enable ? "Start" : "Stop") << " scanning\n";
  bluezProxy->callMethod(enable ? METHOD_START_DISCOVERY : METHOD_STOP_DISCOVERY)
    .onInterface(INTERFACE_ADAPTER)
    .dontExpectReply();
}

void BleSensor::subscribeToInterfacesAdded()
{
  const std::string INTERFACE_OBJ_MGR{ "org.freedesktop.DBus.ObjectManager" };
  const std::string MEMBER_IFACE_ADDED{ "InterfacesAdded" };
  auto interfaceAddedCallback = [this](sdbus::ObjectPath path,
    std::map<std::string,
    std::map<std::string, sdbus::Variant>>
    dictionary)
    {
      const std::regex DEVICE_INSTANCE_RE{ "^/org/bluez/hci[0-9]/dev(_[0-9A-F]{2}){5}_62$" };
      const std::regex DEVICE_ATTRS_RE{ "^/org/bluez/hci\\d/dev(_[0-9A-F]{2}){6}/service\\d{4}/char\\d{4}" };
      std::smatch match;
      std::cout << "(TID: " << std::this_thread::get_id() << ") ";
      if (!connected)
      {
        std::cout << "Current processing path: " << path << '\n';
        if (std::regex_match(path, match, DEVICE_INSTANCE_RE))
        {
          std::cout << "Device iface ";

          if (dictionary["org.bluez.Device1"].count("Name") == 1)
          {
            auto name = (std::string)(dictionary["org.bluez.Device1"].at("Name"));
            std::cout << name << " @ " << path << std::endl;
            if (name == deviceName)
            {
              std::cout << "Connecting to " << name << std::endl;
              connectToDevice(path);
            }
          }
          else
          {
            std::cout << "<NAMELESS> @ " << path << std::endl;
          }
        }
        else
        {
          std::cout << "*** UNEXPECTED SIGNAL ***";
        }
      }
      else
      {
        if (std::regex_match(path, match, DEVICE_ATTRS_RE))
        {
          std::cout << "Characteristic " << path << std::endl;
          if ((dictionary.count("org.bluez.GattCharacteristic1") == 1) &&
            (dictionary["org.bluez.GattCharacteristic1"].count("UUID") == 1))
          {
            auto name = (std::string)(dictionary["org.bluez.GattCharacteristic1"].at("UUID"));

            std::cout << "!!!!!!!!!Current name: " << name << '\n';

            if (name == "0000ffe1-0000-1000-8000-00805f9b34fb")
            {
              tempAttrProxy = sdbus::createProxy(SERVICE_BLUEZ, path);
              std::cout << "<<<FOUND>>> " << path << std::endl;

              const std::string INTERFACE_CHAR{ "org.bluez.GattCharacteristic1" };
              auto connectionCallback = [this](const sdbus::Error* error)
                {
                  if (error != nullptr)
                  {
                    std::cerr << "Got connection error "
                      << error->getName() << " with message "
                      << error->getMessage() << std::endl;
                    return;
                  }
                };

              std::map<std::string, sdbus::Variant> args{ {{"offset", sdbus::Variant{std::uint16_t{0}}}} };
              std::vector<std::uint8_t> result;
              tempAttrProxy->callMethod("StartNotify")
                .onInterface(INTERFACE_CHAR);
              tempAttrProxy->onInterface(INTERFACE_CHAR);
              std::cout << "//----------------WRITTENE------------------//" << std::endl;
            }
            std::cout << name << std::endl;
          }
        }
        else
        {
          std::cout << "Not a characteristic" << std::endl;
        }
      }
    };
  // Let's subscribe for the interfaces added signals (AddMatch)
  rootProxy->uponSignal(MEMBER_IFACE_ADDED).onInterface(INTERFACE_OBJ_MGR).call(interfaceAddedCallback);
  rootProxy->finishRegistration();
}

void BleSensor::connectToDevice(sdbus::ObjectPath path)
{
  const std::string INTERFACE_DEVICE{ "org.bluez.Device1" };
  const std::string METHOD_CONNECT{ "Connect" };
  auto connectionCallback = [this](const sdbus::Error* error)
    {
      if (error != nullptr)
      {
        std::cerr << "Got connection error "
          << error->getName() << " with message "
          << error->getMessage() << std::endl;
        return;
      }
      std::unique_lock<std::mutex> lock(mtx);
      std::cout << "Connected!!!" << std::endl;
      connected = true;
      lock.unlock();
      cv.notify_one();
      std::cout << "Finished connection method call" << std::endl;
    };
  deviceProxy = sdbus::createProxy(SERVICE_BLUEZ, path);
  deviceProxy->callMethodAsync(METHOD_CONNECT).onInterface(INTERFACE_DEVICE).uponReplyInvoke(connectionCallback);
  std::cout << "Connection method started" << std::endl;
}

void BleSensor::writeFromDolboeb()
{
  std::cout << "//----------------START READINGE-----------------//" << std::endl;

  std::map<std::string, sdbus::Variant> args{ {{"offset", sdbus::Variant{std::uint16_t{0}}}} };
  std::vector<std::uint8_t> buffer{ 0xbf, 0x10, 0x00, 0x7a, 0xc5, 0xcc, 0x90, 0x12, 0x15, 0x90, 0x36, 0x92, 0xe6, 0x9b, 0xc1, 0x99, 0xa4, 0x15, 0x8a };

  const std::string INTERFACE_CHAR{ "org.bluez.GattCharacteristic1" };

  tempAttrProxy->callMethod("WriteValue")
    .onInterface(INTERFACE_CHAR)
    .withArguments(buffer, args);

  std::cout << "//----------------WRITTENE------------------//" << std::endl;
}

void BleSensor::readTemperature()
{

}

float BleSensor::valueFromIeee11073(std::vector<std::uint8_t> binary)
{
  float value = static_cast<float>(binary[0]) + static_cast<float>(binary[1]) * 256.f + static_cast<float>(binary[2]) * 256.f * 256.f;
  float exponent;
  if (binary[3] > 127)
  {
    exponent = static_cast<float>(binary[3]) - 256.f;
  }
  else
  {
    exponent = static_cast<float>(binary[3]);
  }
  return value * pow(10, exponent);
}

void BleSensor::disconnectFromDevice()
{
  const std::string INTERFACE_DEVICE{ "org.bluez.Device1" };
  const std::string METHOD_DISCONNECT{ "Disconnect" };

  auto disconnectionCallback = [this](const sdbus::Error* error)
    {
      if (error != nullptr)
      {
        std::cerr << "Got disconnection error " << error->getName() << " with message " << error->getMessage() << std::endl;
        return;
      }
      std::unique_lock<std::mutex> lock(mtx);
      std::cout << "Disconnected!!!" << std::endl;
      connected = false;
      deviceProxy = nullptr;
      lock.unlock();
      std::cout << "Finished connection method call" << std::endl;
    };

  {
    deviceProxy->callMethodAsync(METHOD_DISCONNECT).onInterface(INTERFACE_DEVICE).uponReplyInvoke(disconnectionCallback);
    std::cout << "Disconnection method started" << std::endl;
  }
}