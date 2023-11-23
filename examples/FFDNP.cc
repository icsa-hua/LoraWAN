/*
| This is a simple program to showcase the capabilities of the NS3 and the LoRa protocol for V2I communications. 
| We have a basic node implementation which is the GW and multiple End Devices that represent the moving vehicles. 
| The network can dynamically alocate the end devices, based on a broadcast reply method. If that packet reaches 
| an end device, that device will have to reply and later get accepted by the network. Once it registers, it will 
| remain until the end of the simulation inside the network. The mobility of the end Devices is determined by basic
| movement methods or by a trace file using sumo. In this scenario we have multiple nodes being added to the network, 
| by continuous broadcasting from the gw. The gw does not need to know the addresses of the end devices. It can simply
| send a packet towards all. At a random time interval an emergency event occurs and the network must inform all the 
| end devices it has accepted. After that the system returns to a normal state in which the previous features are in power.
| 
|
| The LoRa Implementation is provided by the University of Padova. 
| Main Authors : Davide Magrin <magrinda@dei.unipd.it>
|                Martina Capuzzo <capuzzom@dei.unipd.it>
| Basic class changes to include a Class C device have been the contribution of Author: QiuYukang <b612n@qq.com> 
*/

#include "ns3/node-container.h"
#include "ns3/log.h"
#include "ns3/command-line.h"
#include "ns3/config-store-module.h" //Basic NS3 imports 
#include "ns3/core-module.h"
#include "ns3/callback.h"
#include "ns3/traced-callback.h"

#include "ns3/lora-channel.h"
#include "ns3/correlated-shadowing-propagation-loss-model.h"

#include "ns3/lora-phy-helper.h"
#include "ns3/lorawan-mac-helper.h" //Channel Helpers based on Layer 
#include "ns3/lora-helper.h"

#include "ns3/forwarder-helper.h" //Network forwarder. GW will forward the packets from and towards the NS
#include "ns3/network-server-helper.h"
#include "ns3/network-module.h"

#include "ns3/mobility-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/position-allocator.h"
#include "ns3/constant-position-mobility-model.h"

#include "ns3/periodic-sender-helper.h" //Application Helpers. 
#include "ns3/one-shot-sender-helper.h"

#include "ns3/building-penetration-loss.h"
#include "ns3/building-allocator.h" //Buildings Helpers. For a more realistic approach. 
#include "ns3/buildings-helper.h"
#include "ns3/netanim-module.h"
#include <random>
#include <fstream>
#include <iostream> //Basic C++ imports 
#include <vector>
#include <ctime>
#include <cstdlib>
#include <map>
#include <boost/bimap.hpp>
#include <tuple> 
#include <queue>

using namespace ns3;
using namespace lorawan; 
using namespace std; 
using namespace boost::bimaps; 

typedef bimap<Ptr<Node>,LoraDeviceAddress> Bimap; // To search with ease 
typedef bimap<int, std::tuple<Time,double,double>> Bimap2; 
NS_LOG_COMPONENT_DEFINE("DynamicGateway");

///RX_P : pending the RX state 
///TX_P : pending the TX state
///RX_F : finished the RX state
///TX_F : finished the TX state

namespace ns3{
    class LoraApp : public Object{
      public:   
        LoraApp(); 
        virtual ~LoraApp(); 
        void Simulate(int argc, char** argv);

        Bimap m_createdNodes;  //Hold the node and its address when it is created. 
        NodeContainer m_rspus; 
        NodeContainer m_vehicles;
        Ptr<NetworkServer> m_ns; 
        OneShotSenderHelper oneShot;    
        PeriodicSenderHelper perShot; 
        ApplicationContainer app;
        ForwarderHelper m_forwarderHelper;
        Time Next_GW_transmission; 
        std::map<Address, Time> Next_GW_transmissions;//This is to allow the next broadcast due to duty cycle. 
        uint32_t stationary_nodes; 
        uint32_t eme_nodes; 
        bool event_occurred; //An emergency event has been detected. 
        std::vector<int> packetsSent; //These are used for later metrics 
        std::vector<int> packetsReceived;
        Ptr<LoraChannel> m_loraChannel; 
        LoraPhyHelper m_phyHelper; 
        LorawanMacHelper m_macHelper; 
        LoraHelper m_loraHelper; 
        NodeContainer sensorNode; 
        double Simulation; 
        int maxReceptionPaths; 
        Time broadcast_time;
        bool Reception_completed; 
        uint32_t events; 
        bool ns_gw_reply_sent;
        bool succ_broadcast;
        int applicable_nodes; 
        std::vector<Ptr<Node>> excluded_nodes; 
        std::vector<std::set<Ptr<Node>>> Subsets;
        std::set<Ptr<Node>> remainingNodes;

        //Measurements 
        uint32_t count_lost_packets_Intf; 
        uint32_t count_lost_packets_DC; 
        uint32_t count_norm_broadcast; 
        uint32_t count_failed_packets;
        uint32_t count_emerg_devices; 
        uint32_t count_emergencies; 

        std::map<LoraDeviceAddress, Ptr<EndDeviceStatus>> m_endDeviceStatuses;
        std::map<Address, Ptr<GatewayStatus>> m_gatewayStatuses;
        std::queue<LoraDeviceAddress> SendQueue; 
        std::map<Ptr<Node>, std::string> vehicles_States;
        std::vector<Address> gwAddresses_vec; 
        Address bestGWAddress;
        LoraDeviceAddress ackAddress; 
        std::queue<uint32_t> phy_end_reception_count; 
      
      protected:
        virtual void ParseCommandLineArguments(int argc, char** argv); 
        virtual Ptr<LoraChannel> ConfigureChannel();
        virtual void ConfigureHelpers(uint32_t state, NodeContainer node); 
        virtual void ConfigureInitialNodes();
        virtual void HandleBuildings(); 
        virtual void RunSimulation(); 
        virtual void ProcessOutputs(); 
    };

    NS_OBJECT_ENSURE_REGISTERED(LoraApp);

    //--- The Methods of the LoRa App class ---//
    LoraApp::LoraApp()
        :packetsSent(6.0),
         packetsReceived(6.0),
         count_lost_packets_Intf(0), 
         count_failed_packets(0), 
         count_lost_packets_DC(0),
         count_emerg_devices(0),
         ns_gw_reply_sent(false), 
         stationary_nodes(1),
         events(0), 
         Reception_completed(false), 
         broadcast_time(Seconds(0)),
         applicable_nodes(0) 
    {
        m_forwarderHelper = ForwarderHelper();
        app = ApplicationContainer(); 
        Next_GW_transmission = Seconds(3); 

        count_emergencies = 0; 
        event_occurred = false;
        stationary_nodes = 1; //The NS node 
        eme_nodes = 0; 
        events = 0; 
        succ_broadcast = false; 
        broadcast_time = Seconds(0); 
    }

    LoraApp::~LoraApp()
    {
    }

    void LoraApp::Simulate(int argc, char** argv){
        //This is the function to handle the basic configurations. 
        //Add here any other methods for configurations. 
        NS_LOG_INFO("|Starting Simulation Configuration...");
        ParseCommandLineArguments(argc, argv); 
    }

    void LoraApp::ParseCommandLineArguments(int argc, char** argv){}
    Ptr<LoraChannel> LoraApp::ConfigureChannel(){return CreateObject<LoraChannel>();}
    void LoraApp::ConfigureHelpers(uint32_t state,NodeContainer node){}
    void LoraApp::ConfigureInitialNodes(){}
    void LoraApp::HandleBuildings(){}
    void LoraApp::RunSimulation(){}
    void LoraApp::ProcessOutputs(){}

 
    
    
    //--- Save or Load A Configuration File to store the initial state and parameters. ---//
    class ConfigStoreHelper{
        public:
            ConfigStoreHelper(); 
            void LoadConfig(std::string configFilename);
            void SaveConfig(std::string configFilename); 
    };

    ConfigStoreHelper::ConfigStoreHelper(){}
    void ConfigStoreHelper::LoadConfig(std::string configFilename){
        Config::SetDefault("ns3::ConfigStore::Filename",StringValue(configFilename)); 
        Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("RawText")); 
        Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Load")); 
        ConfigStore inputConfig;
        inputConfig.ConfigureDefaults(); 
    }

    void ConfigStoreHelper::SaveConfig(std::string configFilename){
        if(!configFilename.empty()){
            Config::SetDefault("ns3::ConfigStore::Filename",StringValue(configFilename)); 
            Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("RawText")); 
            Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
            ConfigStore outputConfig; 
            outputConfig.ConfigureDefaults(); 
        }
    }

       //--- Instance of a LoRa App class to handle the parameters during execution. ---//
    LoraApp lora_app = LoraApp();

    //------------------------------------- --------------------------------------//
    //--- Callback Functions Used to Handle Transmissions for both GW and EDs  ---//
    //------------------------------------- --------------------------------------//
   
    void SentNewPacketCallback(Ptr<Packet const> packet){
        LorawanMacHeader mHdr; 
        LoraFrameHeader fHdr; 
        Ptr<Packet> myPacket = packet->Copy();
        myPacket->RemoveHeader(mHdr);
        myPacket->RemoveHeader(fHdr);
        LoraTag tag;
        packet->PeekPacketTag(tag);
        NS_LOG_FUNCTION("A new Packet was successfully sent into the MAC layer :: "
                        << packet
                        << " From Address: " << fHdr.GetAddress()
                        << " Sent over with Frequency: " << tag.GetFrequency());
        lora_app.succ_broadcast = true; 
        //Only works for a single GW for the time being. 
        if((fHdr.GetAddress()).IsBroadcast()){
            NS_LOG_FUNCTION("A new Packet was successfully broadcasted into the MAC layers :: "
                        << packet
                        << " From GW Address: " << fHdr.GetAddress()
                        << " With Frequency: " << tag.GetFrequency());
            for(uint32_t node = 0; node <lora_app.gwAddresses_vec.size(); node++){
                lora_app.Next_GW_transmissions[lora_app.gwAddresses_vec.at(node)] = lora_app.m_rspus.Get(node)->GetDevice(0)->GetObject<LoraNetDevice>() -> GetMac()->GetObject<GatewayLorawanMac>()-> GetWaitingTime(869.525);
                NS_LOG_DEBUG("The Gateway " <<lora_app.gwAddresses_vec.at(node) << " will be available to broadcast again after :: " << lora_app.Next_GW_transmissions[lora_app.gwAddresses_vec.at(node)].GetSeconds() ); 
            }

        }else{
            NS_LOG_FUNCTION("A new ACK Packet was successfully unicasted into the MAC layers :: "
                        << packet
                        << " From GW Address: " << fHdr.GetAddress()
                        << " With Frequency: " << tag.GetFrequency());
            lora_app.ackAddress = fHdr.GetAddress(); 
        }
    }

    //--- Assess the gateways and if they can broadcast or unicast a message to the devices ---// 
    std::map<Address,Ptr<NetDevice>> AvailableGWToSend(NodeContainer gateway, LoraDeviceAddress address, int window){
        NS_LOG_FUNCTION(lora_app.m_rspus.Get(0)); 
        if (address.IsBroadcast()){
            std::map<Address,Ptr<NetDevice>> gwAddresses;
            NS_LOG_FUNCTION(gateway.Get(0)); 
            for(NodeContainer::Iterator jj = gateway.Begin(); jj != gateway.End(); jj++){
                Ptr<Node> gwNode = (*jj);
                Ptr<NetDevice> netDevice = gwNode->GetDevice(0); 
                Ptr<PointToPointNetDevice> p2pNetDevice; 
                for (uint32_t i = 0; i < gwNode->GetNDevices(); i++){
                    p2pNetDevice = gwNode->GetDevice (i)->GetObject<PointToPointNetDevice> ();
                    if (p2pNetDevice != 0){
                        break;
                    }
                }  
                Ptr<GatewayLorawanMac> gwMac = netDevice->GetObject<LoraNetDevice>()->GetMac()->GetObject<GatewayLorawanMac>(); 
                netDevice->GetObject<LoraNetDevice>()->GetMac() -> TraceConnectWithoutContext("SentNewPacket",MakeCallback(&ns3::SentNewPacketCallback)); 
                
                NS_ASSERT(gwMac!=0); 
                Address gwAddress = p2pNetDevice->GetAddress();
                Ptr<GatewayStatus> gwStatus = Create<GatewayStatus>(gwAddress, netDevice, gwMac); 
                lora_app.m_gatewayStatuses.insert(pair<Address,Ptr<GatewayStatus>>(gwAddress,gwStatus)); 
                lora_app.gwAddresses_vec.push_back(gwAddress); 
                if((gwStatus->IsAvailableForTransmission(869.525))){
                    gwAddresses.insert(pair<Address,Ptr<NetDevice>>(gwAddress, netDevice)); 
                }
            }
            return gwAddresses;
        }else {
            //Unicast Method 
            Ptr<EndDeviceStatus> edStatus = lora_app.m_endDeviceStatuses.at(address); 
            double replyFrequency; 
            if (window == 1){
                replyFrequency = edStatus->GetFirstReceiveWindowFrequency(); 
            }else if(window == 2){
                replyFrequency = edStatus->GetSecondReceiveWindowFrequency(); 
            }else{
                NS_ABORT_MSG("Invalid window input"); 
            }

            std::map<double, Address> gwAddresses = edStatus->GetPowerGatewayMap();
            std::map<Address,Ptr<NetDevice>> bestGWAddress;
            for(auto it = gwAddresses.rbegin(); it!=gwAddresses.rend(); it++){
                bool isAvailable = lora_app.m_gatewayStatuses.find(it->second)->second -> IsAvailableForTransmission(replyFrequency); 
                if(isAvailable){
                    bestGWAddress.insert(pair<Address,Ptr<NetDevice>>((it->second),(lora_app.m_gatewayStatuses.find(it->second)->second ->GetNetDevice())));
                    break; 
                }
            }
            return bestGWAddress;

        }
    }

    void CreateSubsets(){
        
        NS_LOG_FUNCTION_NOARGS();
        //Function to create the subsets based on distance. 
        std::vector<std::set<Ptr<Node>>> subsets; 
        std::set<Ptr<Node>> set1; 
        subsets.push_back(set1);
        subsets.push_back(set1);
        subsets.push_back(set1);
        subsets.push_back(set1);
        subsets.push_back(set1);
        Ptr<MobilityModel> gwMob = lora_app.m_rspus.Get(0)->GetObject<MobilityModel>(); 
        for(auto edNode = lora_app.m_vehicles.Begin(); edNode != lora_app.m_vehicles.End() ; edNode++){
            Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = (*edNode)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac() ->GetObject<ClassCEndDeviceLorawanMac>();
            double distance = gwMob->GetDistanceFrom((*edNode)->GetObject<MobilityModel>());
            NS_LOG_FUNCTION(distance);

            if(distance<1000.0){
                (subsets[0]).insert((*edNode));
                lora_app.applicable_nodes++; 
                edLorawanMac->SetDataRate(5);
            }else if (distance>=1000.0 && distance<2400.0){
                (subsets[1]).insert((*edNode));
                lora_app.applicable_nodes++; 
                edLorawanMac->SetDataRate(4);
            }else if (distance>=2400.0 && distance<3800.0){
                (subsets[2]).insert({*edNode});
                lora_app.applicable_nodes++; 
                edLorawanMac->SetDataRate(3);
            }else if (distance>=3800.0 && distance<5200.0){
                (subsets[3]).insert((*edNode));
                lora_app.applicable_nodes++; 
                edLorawanMac->SetDataRate(2);
            }else if (distance>=5200.0 && distance<6400.0){
                (subsets[4]).insert((*edNode));  
                lora_app.applicable_nodes++;     
                edLorawanMac->SetDataRate(0); 
            }else{
                lora_app.excluded_nodes.push_back(*edNode);
                //(*edNode)->Dispose(); 
            }
        }
        NS_LOG_DEBUG("APPLICABLE DEVICES AMOUNT TOOO :::: " << lora_app.applicable_nodes );
        lora_app.Subsets = subsets;  

        for(uint32_t ii =0; ii < 5 ; ii++){
            for(auto node : lora_app.Subsets[ii]){
                lora_app.remainingNodes.insert(node); 
            }
        }
        NS_LOG_DEBUG("Remaining Nodes :: " << lora_app.remainingNodes.size()); 

    }

    //--- Create the Necessary Packets for Eds and GWs ---//
    //The Broadcast message of the GW. 
    Ptr<Packet> CreateBroadcastPacket(Ptr<Packet> data){ 
        LoraDeviceAddress address;
        address.SetNwkID (0x7F);
        address.SetNwkAddr (0x1FFFFFF);
        LorawanMacHeader mHdr;
        mHdr.SetMType (LorawanMacHeader::UNCONFIRMED_DATA_DOWN);
        mHdr.SetMajor (1);
        LoraFrameHeader fHdr;
        fHdr.SetAsDownlink ();
        fHdr.SetFPort (1); 
        fHdr.SetAddress (address);
        fHdr.SetAdr (false);
        fHdr.SetAck (false);
        fHdr.SetAdrAckReq (0);
        fHdr.SetFCnt (0);
        fHdr.SetFPending (false);
        Ptr<Packet> packet = data->Copy();
        LoraTag tag;
        tag.SetFrequency (869.525);
        tag.SetDataRate (0);
        packet->AddHeader (fHdr);
        packet->AddHeader (mHdr);
        packet->AddPacketTag (tag);
        NS_LOG_DEBUG("Broadcast packet created succesfully!!");
        return packet; 
    }

    //--- Have the Network Server send the broadcast emergency packet ---//
    void NSSendPacket(Ptr<NetworkServer> server, Ptr<Packet> pkt, LoraDeviceAddress endDevice) {

        server->Send(pkt, endDevice,2);
        if (endDevice.IsBroadcast()){
            lora_app.count_norm_broadcast++;
            NS_LOG_INFO("Broadcast Transmission From NS is successfull at time(s) : " << (Simulator::Now()).GetSeconds());
        }
        if (endDevice.IsBroadcast()){
            for(uint32_t node = 0; node < lora_app.gwAddresses_vec.size(); node++){
                lora_app.Next_GW_transmissions[lora_app.gwAddresses_vec.at(node)] = lora_app.m_rspus.Get(node)->GetDevice(0)->GetObject<LoraNetDevice>() -> GetMac()->GetObject<GatewayLorawanMac>()-> GetWaitingTime(869.525);
            }
            lora_app.broadcast_time = Simulator::Now(); 
            NS_LOG_INFO("Broadcast Transmission From NS is successfull at time(s) : " << lora_app.broadcast_time.GetSeconds());
        }
    }

    void PhyRxBegin(Ptr<Packet const> packet){
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        Ptr<Packet> myPacket = packet->Copy();
        myPacket->RemoveHeader(mHdr);
        myPacket->RemoveHeader(fHdr);
        LoraDeviceAddress address = fHdr.GetAddress (); //Get the address of the ED that sent the packet. 
        NS_LOG_FUNCTION(address);
    }

    void OnReceivePacketGW (Ptr<Packet const> packet, uint32_t systemId){
        NS_LOG_FUNCTION(packet << systemId); 
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        Ptr<Packet> myPacket = packet->Copy();
        myPacket->RemoveHeader(mHdr);
        myPacket->RemoveHeader(fHdr);
        LoraDeviceAddress address = fHdr.GetAddress (); //Get the address of the ED that sent the packet. 
        LoraTag tag;
        myPacket->PeekPacketTag(tag);
        lora_app.packetsReceived.at(tag.GetSpreadingFactor()-7)++; 
        myPacket->RemovePacketTag(tag);

        NS_LOG_DEBUG ("Correctly Received Packet From ED: " << fHdr.GetAddress()
                      <<" at time (s): " << (Simulator::Now()).GetSeconds()
                      <<" at frequency : " << tag.GetFrequency());   
        Ptr<NetworkStatus> net_status = lora_app.m_ns->GetNetworkStatus(); 
        std::map<LoraDeviceAddress, Ptr<EndDeviceStatus>> endDeviceStatuses = net_status->m_endDeviceStatuses;
        if(endDeviceStatuses.find(address)==endDeviceStatuses.end()){
            Bimap::right_const_iterator it = lora_app.m_createdNodes.right.find(address); //Get the node of that address.
            //This will be the first time the node succesfully sends a packet to the gw 
            if (it != lora_app.m_createdNodes.right.end()){
                //The node is found in the created Nodes bimap. 
                lora_app.vehicles_States.erase((it->second)); // Update the state of the end Device. 
                lora_app.vehicles_States.insert(pair<Ptr<Node>, std::string>((it->second),"TX_F")); //The TX has finished succesfully. 
                
                //Add the node to the network and to the edStatus map. 
                //Check to see if the end device is already at the network 
                if (lora_app.m_endDeviceStatuses.find(it->first) == lora_app.m_endDeviceStatuses.end()){
                    Ptr<EndDeviceLorawanMac> edMacWan = (it->second)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>(); 
                    Ptr<EndDeviceStatus> edstatus = CreateObject<EndDeviceStatus>(address,edMacWan);
                    //Add it to the queue for later send reply 
                    lora_app.SendQueue.push(it->first); 
                    //Add the node to the network 
                    lora_app.m_ns->AddNode((it->second));
                    //Stop sending information as ED
                    lora_app.m_endDeviceStatuses.insert(pair<LoraDeviceAddress, Ptr<EndDeviceStatus>>(address,edstatus)); 
                    //lora_app.perShot.SetPeriod(Seconds(lora_app.Simulation*2)); 
                    //lora_app.perShot.Install(it->second);
                    //Ptr<PeriodicSender> app = (it->second)->GetApplication(0)->GetObject<PeriodicSender>();
                    //app->SetInterval(Seconds(lora_app.Simulation*2));
                }
            
                // Delete that node from the created ones. 
                std::string vehicle_state ("TX_F"); 
                if (!vehicle_state.compare(lora_app.vehicles_States[it->second])){
                    lora_app.m_createdNodes.right.erase(it->first); 
                } 
            }
        }
        

        if (lora_app.m_createdNodes.empty()&&(lora_app.m_endDeviceStatuses.size() == lora_app.m_vehicles.GetN())&&(!lora_app.Reception_completed)){
            lora_app.Reception_completed = true;
            NS_LOG_DEBUG("All Nodes have been added to the Network! | No more packets are being received...");
            NS_LOG_DEBUG("Nodes that have been Added to the network => " << lora_app.m_ns->GetNetworkStatus()->CountEndDevices());
            NS_LOG_DEBUG("Number Of nodes that have been created => " << lora_app.m_vehicles.GetN());
            lora_app.Next_GW_transmission = Simulator::Now() + Seconds(5); 
            
            //Simulator::Schedule(lora_app.Next_GW_transmission,&ns3::NSSendPacket, lora_app.m_ns,Create<Packet>(10),LoraDeviceAddress(0xFF, 0xFFFFFFFF));
            
            /*Simulator::Schedule(lora_app.Next_GW_transmission + Seconds(1),[](){
                for(NodeContainer::Iterator node = lora_app.m_vehicles.Begin(); node != lora_app.m_vehicles.End(); node++){
                    Ptr<EndDeviceLorawanMac> edMacWan = (*node)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>(); 
                    LoraDeviceAddress loraAddress = edMacWan->GetDeviceAddress(); 
                    Ptr<EndDeviceStatus> edstatus = CreateObject<EndDeviceStatus>(loraAddress,edMacWan);
                    lora_app.m_endDeviceStatuses[loraAddress] = edstatus; 
                    Ptr<PeriodicSender> app = (*node)->GetApplication(0)->GetObject<PeriodicSender>();
                    app->StopApplication();
                    (*node)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<ClassCEndDeviceLorawanMac>()->OpenSecondReceiveWindow(true);
                }
            });*/

            NS_LOG_DEBUG("Reply to all nodes that have been accepted in the network was successfully completed!"); 
        }

        /*f(lora_app.event_occurred && address == (lora_app.sensorNode.Get(0)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>()->GetDeviceAddress())){

            //Simulator::Schedule(Seconds(0), &ns3::NSSendPacket, lora_app.m_ns, Create<Packet>(10), LoraDeviceAddress(0xFF, 0xFFFFFFFF));       
            NS_LOG_DEBUG("RECEIVED EMERGENCY PACKET AND TRANSMITTING IT TO THE ENTIRE NETWORK RIGHT AWAY..."); 
            return; 
        }*/
    }


    //--- Reception of Packets Is Interrupted by interference ---//
    void OnReceiveInterferenceGW(Ptr<Packet const> packet, uint32_t systemId){
       // NS_LOG_FUNCTION(packet<<systemId);
        lora_app.count_lost_packets_Intf++; 
    }

    //--- Callback to determine wheather a packet has been sent to the mac layer ---//
    void FinishedTransmissionCallback(uint8_t transmissions, bool successful, Time firstAttempt, Ptr<Packet> packet){    
        //NS_LOG_FUNCTION(transmissions << successful << firstAttempt << packet); 
        if(!successful){
            lora_app.count_failed_packets++;
        }
    }

    //--- Successfull transmission of packet from the ED towards the GW ---//
    void PhySentEdCallback (Ptr<const Packet> packet, uint32_t index) {
        //NS_LOG_FUNCTION (packet << index);
        Ptr<Packet> packetCopy = packet->Copy();
        LoraTag tag;
        packet->PeekPacketTag(tag);
        lora_app.packetsSent.at(tag.GetSpreadingFactor()-7)++;
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        packetCopy->RemoveHeader (mHdr);
        packetCopy->RemoveHeader (fHdr);
                LoraDeviceAddress address = fHdr.GetAddress();

        NS_LOG_DEBUG ("Correctly Sent Packet From ED: "<<fHdr.GetAddress() << " at time (s): " << (Simulator::Now()).GetSeconds() << " and in Frequency " << tag.GetFrequency()); 
        lora_app.vehicles_States[(address)] = "TX_F";
    }


    void AfterReceptionTransmission(Ptr<Packet> data,Ptr<Node> edNode,LoraDeviceAddress address){

        NS_LOG_DEBUG ("Correctly Received Packet " << data << " From GW at time (s): " <<(Simulator::Now()).GetSeconds()); 
        lora_app.vehicles_States.erase(edNode); 
        lora_app.vehicles_States.insert(pair<Ptr<Node>, std::string>(edNode,"TX_P"));
        //Physical Layer handles the State (SLEEP, TX< STANDBY, RX) and the physical trace sources
        Ptr<LoraPhy> Phy = edNode->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy();
        Ptr<EndDeviceLoraPhy> edPhy = edNode->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<EndDeviceLoraPhy>(); 
        Ptr<LorawanMac> mac = edNode->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac();
        Ptr<EndDeviceLorawanMac> edMac =  mac->GetObject<EndDeviceLorawanMac>();
        Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = mac->GetObject<ClassCEndDeviceLorawanMac>(); 
        Ptr<EndDeviceStatus> edstatus = CreateObject<EndDeviceStatus>(address,edMac);
        Address gwAddress = lora_app.m_gatewayStatuses.begin()->first;
        edstatus->InsertReceivedPacket(data, gwAddress);

        if (edPhy->GetState() == EndDeviceLoraPhy::STANDBY){
            edLorawanMac->SetMType (LorawanMacHeader::UNCONFIRMED_DATA_UP); 
            //Time Delay = edLorawanMac->GetNextClassTransmissionDelay(Seconds(0)) + Simulator::Now();
            //NS_LOG_FUNCTION("DELAY IS ::: "<< Delay);  
            Phy -> TraceConnectWithoutContext("StartSending", MakeCallback(&ns3::PhySentEdCallback));
            edMac -> TraceConnectWithoutContext("RequiredTransmissions", MakeCallback(&ns3::FinishedTransmissionCallback)); 
            Simulator::Schedule(Seconds(0), [edNode](){
                Time delay = Seconds(edNode->GetId());
                lora_app.perShot.SetPeriod((Seconds(lora_app.Simulation/2))); 
                lora_app.perShot.SetPacketSize(10); 
                lora_app.perShot.Install(edNode);
                Ptr<PeriodicSender> app = edNode->GetApplication(0)->GetObject<PeriodicSender>();
                app->SetInitialDelay(delay);
                //app->SetInterval(Seconds(60)); 
                app->StartApplication(); 
            });
        }
    }

    void CorrectReceptionEDCallback(Ptr<Packet const> packet, uint32_t recepientId){
        //PAcketWasRecieved Correclty :: 
        NS_LOG_FUNCTION(packet<<recepientId);
        Ptr<Packet> myPacket = packet->Copy();

        Ptr<Node> cur_node = lora_app.m_vehicles.Get(recepientId-lora_app.stationary_nodes); //The node that received the pkt  
        LoraDeviceAddress addr = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>()->GetDeviceAddress(); 
        
        std::string vehicle_state ("TX_F");
        if(lora_app.event_occurred){ 
            //NS_LOG_FUNCTION("Correctly Received the Emergency packet from the NS... "); 
            lora_app.count_emerg_devices++; 
            auto enddevice = --(lora_app.m_endDeviceStatuses.end()); 
            if (enddevice->first == addr){
                //In the queue there will be the final devices that entered the network, even though the reply packet has not been sent yet. 
                 Simulator::Schedule(Seconds(0.25),[](){lora_app.event_occurred = false;}); //Signal to terminate the event since all available net devices received the message. 
            }
            return;
        }

        if (!vehicle_state.compare(lora_app.vehicles_States[cur_node])){
            lora_app.SendQueue.pop();
            lora_app.vehicles_States.erase(cur_node); 
            lora_app.vehicles_States.insert(pair<Ptr<Node>, std::string>(cur_node,"RX_P"));
            cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<ClassCEndDeviceLorawanMac>()->OpenSecondReceiveWindow(true); 
            return; 
        } 
        Ptr<EndDeviceLorawanMac> edLorawanMac = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>();        

        //if device already in the network the do not do anythin for now. 
        if (lora_app.m_endDeviceStatuses.find(addr)!=lora_app.m_endDeviceStatuses.end()){return;}

        //If the device just received the first message from the GWs then it will schedule a reply. 
        lora_app.vehicles_States.erase(cur_node); 
        lora_app.vehicles_States[(cur_node)] = "RX_F"; //Finished receiving the packet.

        //NS_LOG_DEBUG("The vehicle state :: " << lora_app.vehicles_States[(cur_node)]);
        Simulator::Schedule(Seconds(3), &ns3::AfterReceptionTransmission, myPacket, cur_node,addr);
        return; 
    }

    //--- Create the Emergency Event to broadcast the packet towards the EDs at a random time something is detected. ---//
    void CreateEvent(Ptr<Packet> packet){
        //Have a new remote node send an emergency message to the NS and the nerwork server replying to all the devices inside the network at that point in time 
        lora_app.count_emergencies++;
        lora_app.event_occurred = true;  
     
        lora_app.eme_nodes = lora_app.m_ns->GetNetworkStatus()->CountEndDevices();
        NS_LOG_FUNCTION("An emergency event id was triggered, sensor reacted to anomaly...");
        //Simulator::Schedule(Seconds(1), [sensorNode](){sensorNode.Get(0)->Dispose();}); 
        //Simulator::Schedule(Seconds(1.2),&ns3::NSSendPacket, lora_app.m_ns, packet, LoraDeviceAddress(0xFF, 0xFFFFFFFF));
        for(auto& net_node : lora_app.m_endDeviceStatuses){
            NS_LOG_DEBUG("Sending Emergency Packet to device with address :: "<<net_node.first); 
            ns3::NSSendPacket(lora_app.m_ns, Create<Packet>(10), net_node.first); 
        }
    }

    //--- Callback Function to handle the GW broadcast towards the end devices ---//
    void SendBroadcastGW(Ptr<Packet> data, NodeContainer gateway){
        std::map<Address,Ptr<NetDevice>> gwAddresses = AvailableGWToSend(gateway, LoraDeviceAddress(0xFF, 0xFFFFFFFF),2); 
        for (auto& address : gwAddresses){
            Ptr<Packet> packet = CreateBroadcastPacket(data); 
            (address.second)->Send(packet,address.first,0x0800);
            Ptr<GatewayLorawanMac> gwMac = (address.second)->GetObject<LoraNetDevice>()->GetMac()->GetObject<GatewayLorawanMac>(); 
            lora_app.Next_GW_transmission = gwMac -> GetWaitingTime(869.525) + Simulator::Now();
            gwMac -> TxFinished(packet);
        }
        lora_app.broadcast_time = Simulator::Now(); 
    }

    void OccupiedReceptionPaths(int m_receptionPaths, int systemID){
        if (systemID==0){
            Ptr<Node> gateway = lora_app.m_rspus.Get(0); 
            Ptr<GatewayLoraPhy> gwPhy = gateway->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<GatewayLoraPhy>(); 
            gwPhy->ResetReceptionPaths(); 
            int m_availablePaths = 0; 
            while(m_availablePaths<8){
                gwPhy->AddReceptionPath(); 
                m_availablePaths++; 
            }
        }
    }

    class DynamicGateway : public LoraApp{
      public:
        DynamicGateway(); 
        void RunSimulation() override;
        static void ConfigureSimulation(void* classInstance); 

    protected:
        void ParseCommandLineArguments(int argc, char** argv) override;
        Ptr<LoraChannel> ConfigureChannel() override; 
        void ConfigureHelpers(uint32_t state,NodeContainer node) override; 
        void ConfigureInitialNodes() override; 
        void HandleBuildings() override;   
        void ProcessOutputs() override;
    
    private: 
        NodeContainer CreateNodes(uint32_t state,bool traceMobility,int index_before,uint32_t number_nodes,std::string tracefile,double radius);
        void EnableLogging(); 
        void ConfigureDefaults();
        void SetupScenario();
        void WriteCsvHeader();
        void SetConfigFromGlobals();
        void SetGlobalsFromConfig();
        void Run();
        
        std::string m_CsvFileName; 
        std::string m_traceFile; 
        std::string m_logFile;
        std::ofstream m_os;
        std::string m_loadConfigFileName; 
        std::string m_saveConfigFileName; 
        uint8_t m_nwkId; 
        uint32_t m_nwkAddr;
        uint32_t m_initial_nodes; 
        uint32_t m_nNodes;
        uint32_t m_nRspu; 
        uint32_t m_packetSize;
        uint32_t m_emergency_time;
        double m_TotalSimTime;
        double m_radius; 
        
        bool m_buildings;
        bool m_realisticChannel; 
        bool m_log; 
        bool m_verbose; 
        bool m_adr; 

        
        Ptr<LoraDeviceAddressGenerator> m_addrGen; 
        NodeContainer m_networkServer;
        NetworkServerHelper m_networkServerHelper; 
    };

    DynamicGateway::DynamicGateway()
        :m_CsvFileName("Lora-experiment.output.csv"),  
        m_traceFile(""),
        m_logFile(""), 
        m_nNodes(1), 
        m_nRspu(1), 
        m_TotalSimTime(3800.0), 
        m_packetSize(23), 
        m_verbose(false),
        m_loadConfigFileName("lora_load_output.txt"), 
        m_saveConfigFileName("lora_load_output"),
        m_realisticChannel(false),
        m_radius(6400),
        m_adr(false),
        m_emergency_time(0)

    {   
        m_log = true; 
        lora_app.m_loraChannel = ConfigureChannel(); 
        lora_app.oneShot = OneShotSenderHelper(); 
        lora_app.perShot = PeriodicSenderHelper(); 
        lora_app.m_loraHelper.EnablePacketTracking();
        m_initial_nodes = 201 ; 
        m_nwkId = 54; 
        m_nwkAddr = 1864; 
    }

    //--- Decleration of global variables ---//
    static ns3::GlobalValue g_nNodes("LEnNodes","Number of Vehicles from trace file",ns3::UintegerValue(203), ns3::MakeUintegerChecker<uint32_t>());
    static ns3::GlobalValue g_nRspu("LEnRspu","Number of RSPUs in the network",ns3::UintegerValue(5),ns3::MakeUintegerChecker<uint32_t>());
    static ns3::GlobalValue g_packetSize("LEpacketSize","Pcaket size in bytes",ns3::UintegerValue(50), ns3::MakeUintegerChecker<uint32_t>());
    static ns3::GlobalValue g_totalSimTime("LEtotalSimTime","Total Sim time (s)",ns3::DoubleValue(3800.0), ns3::MakeDoubleChecker<double>());
    static ns3::GlobalValue g_CsvFileName ("LECsvFileName","CSV filename (for time series data)",ns3::StringValue("Lora-experiment.output.csv"), ns3::MakeStringChecker());
    static ns3::GlobalValue g_traceFile("LEtraceFile","Mobility trace filename",ns3::StringValue("/home/jimborg/sumo/tools/2023-06-13-10-54-49/traffic_mob.tcl"), ns3::MakeStringChecker());
    static ns3::GlobalValue g_logFile("LElogFIle","Log filename",ns3::StringValue("log-lora-net-end-c.filt.7.adj.log"), ns3::MakeStringChecker());
    static ns3::GlobalValue g_realisticChannel("LErealisticChannel","Realistic channel propagation",ns3::BooleanValue(false), ns3::MakeBooleanChecker());
    static ns3::GlobalValue g_verbose("LEverbose","Enable verbose",ns3::BooleanValue(false), ns3::MakeBooleanChecker());
    static ns3::GlobalValue g_radius("LEradius","Available radius for communications",ns3::DoubleValue(6400.0), ns3::MakeDoubleChecker<double>());
    static ns3::GlobalValue g_buildings("LEbuildings","Enable creation of buildings for grid",ns3::BooleanValue(false), ns3::MakeBooleanChecker());
    static ns3::GlobalValue g_emergency_time("LEemergency_time", "The times which an emergency will occur in the system",ns3::UintegerValue(0), ns3::MakeUintegerChecker<uint32_t>()); 

    //--- Enable Logging for the simulation. Use export NS_LOG=DynamicGateway ---//
    void DynamicGateway:: EnableLogging(){
        NS_LOG_FUNCTION("Enabling Logging for the simulation. Use export NS_LOG=DynamicGateway"); 
        LogComponentEnable("DynamicGateway", LOG_LEVEL_ALL);
        //LogComponentEnable("NetworkServer", LOG_LEVEL_ALL);  
        //LogComponentEnable("NetworkScheduler", LOG_LEVEL_ALL);
        //LogComponentEnable("NetworkStatus", LOG_LEVEL_ALL); 
        //LogComponentEnable("GatewayLorawanMac", LOG_LEVEL_ALL);
        //LogComponentEnable("GatewayStatus",LOG_LEVEL_ALL);
        //LogComponentEnable("LoraNetDevice", LOG_LEVEL_ALL);
        //LogComponentEnable("LogicalLoraChannelHelper", LOG_LEVEL_ALL);
        //LogComponentEnable("NetworkStatus", LOG_LEVEL_ALL);
        //LogComponentEnable("NetworkController", LOG_LEVEL_ALL);
        //LogComponentEnable("LoraChannel", LOG_LEVEL_INFO); //*******************
        //LogComponentEnable("LoraPhy", LOG_LEVEL_ALL);
        //LogComponentEnable("EndDeviceLoraPhy", LOG_LEVEL_ALL);
        //SLogComponentEnable("GatewayLoraPhy", LOG_LEVEL_ALL);
        //LogComponentEnable("SimpleGatewayLoraPhy", LOG_LEVEL_ALL);
        //LogComponentEnable("LoraPhyHelper", LOG_LEVEL_ALL);
        //LogComponentEnable("SimpleEndDeviceLoraPhy", LOG_LEVEL_ALL);
        //LogComponentEnable("LoraInterferenceHelper", LOG_LEVEL_ALL);
        //LogComponentEnable("LorawanMac", LOG_LEVEL_ALL);
        //LogComponentEnable("LoraDeviceAddressGenerator", LOG_LEVEL_ALL);
        //LogComponentEnable("EndDeviceLorawanMac", LOG_LEVEL_ALL);
        //LogComponentEnable("EndDeviceStatus", LOG_LEVEL_ALL);
        //LogComponentEnable("LogicalLoraChannel", LOG_LEVEL_ALL);
        //LogComponentEnable("LoraHelper", LOG_LEVEL_ALL);
        //LogComponentEnable("LorawanMacHelper", LOG_LEVEL_ALL);
        //LogComponentEnable("PeriodicSenderHelper", LOG_LEVEL_ALL);
        //LogComponentEnable("PeriodicSender", LOG_LEVEL_ALL);
        //LogComponentEnable("LorawanMacHeader", LOG_LEVEL_ALL);
        //LogComponentEnable("LoraFrameHeader", LOG_LEVEL_ALL);
        //LogComponentEnable("ClassCEndDeviceLorawanMac", LOG_LEVEL_ALL);
        //LogComponentEnable("OneShotSender", LOG_LEVEL_ALL);
        //LogComponentEnable("Ns2MobilityHelper", LOG_LEVEL_DEBUG); 
        //LogComponentEnable("LoraPacketTracker", LOG_LEVEL_ALL);
        LogComponentEnable("AnimationInterface", LOG_LEVEL_ALL);
        m_os.open(m_logFile); 
        Packet::EnablePrinting(); 
    }

    void DynamicGateway::ParseCommandLineArguments(int argc, char** argv){
        NS_LOG_FUNCTION("Enabling commands to be parsed!"); 
        EnableLogging();

        CommandLine cmd(__FILE__); 
        cmd.AddValue("CsvFileName", "The name og the output file name",m_CsvFileName); 
        cmd.AddValue("totalSimTime", "Simulation End Time (s)", m_TotalSimTime); 
        cmd.AddValue("Vehicles", "The number of moving nodes", m_nNodes); 
        cmd.AddValue("RSPUs", "The number of RSPUs",m_nRspu); 
        cmd.AddValue("traceFile","NS2 Movement trace file", m_traceFile );  
        cmd.AddValue("logFile","Log File", m_logFile); 
        cmd.AddValue("verbose","Enable verbose",m_verbose ); 
        cmd.AddValue("Pkt Size", "Lora Packer Size (bytes)",m_packetSize ); 
        cmd.AddValue("loadconfig", "Config-store filename to load",m_loadConfigFileName ); 
        cmd.AddValue("saveconfig", "Config-store filename to save",m_saveConfigFileName ); 
        cmd.AddValue("realisticChannel", "A more realistic approach for the channel including shadowing effects", m_realisticChannel); 
        cmd.AddValue("radius","Lora coommunication radius ", m_radius); 
        cmd.AddValue("buildings","Creation of buildings inside a specified grid", m_buildings);  
        cmd.AddValue("emergencyTimes", "The number of emergencies that will occur in the system" , m_emergency_time); 
        cmd.Parse(argc,argv); 

        ConfigStoreHelper configStoreHelper; 
        configStoreHelper.LoadConfig(m_loadConfigFileName); 
        SetConfigFromGlobals(); 
        cmd.Parse(argc,argv); 

        ConfigureDefaults(); 
        SetGlobalsFromConfig();
        configStoreHelper.SaveConfig(m_saveConfigFileName);

        m_traceFile = "/home/jimborg/sumo/tools/2023-06-13-10-54-49/traffic_mob.tcl";

        m_logFile = "log-lora-net-end-c.filt.7.adj.log" ;
        m_nNodes = 201; 
        m_nRspu = 1; 
        lora_app.stationary_nodes += m_nRspu; 
        m_TotalSimTime = 3800.0;
        lora_app.Simulation =  m_TotalSimTime; 
        m_CsvFileName = "lora-experiment.csv";
        m_buildings = false;
        m_adr = true; 
        m_emergency_time = 7; 
        if (m_buildings){
            HandleBuildings();
        }
        ConfigureInitialNodes(); 
    }

    //--- Setting the configuration file from global class variables and initialize them from cmd  ---//
     void DynamicGateway::SetConfigFromGlobals(){
        NS_LOG_FUNCTION("Setting Configuration from Globals."); 
        UintegerValue uintegerValue; 
        DoubleValue doubleValue; 
        StringValue stringValue; 
        BooleanValue booleanValue; 

        GlobalValue::GetValueByName("LEnNodes",uintegerValue); 
        m_nNodes = uintegerValue.Get(); 
        GlobalValue::GetValueByName("LEnRspu",uintegerValue); 
        m_nRspu = uintegerValue.Get(); 
        GlobalValue::GetValueByName("LEpacketSize",uintegerValue); 
        m_packetSize = uintegerValue.Get(); 
        GlobalValue::GetValueByName("LEverbose",booleanValue); 
        m_verbose = booleanValue.Get(); 
        GlobalValue::GetValueByName("LEtotalSimTime",doubleValue); 
        m_TotalSimTime = doubleValue.Get(); 
        GlobalValue::GetValueByName("LECsvFileName",stringValue); 
        m_CsvFileName = stringValue.Get(); 
        GlobalValue::GetValueByName("LEtraceFile",stringValue); 
        m_traceFile = stringValue.Get(); 
        GlobalValue::GetValueByName("LElogFIle",stringValue); 
        m_logFile = stringValue.Get(); 
        GlobalValue::GetValueByName("LErealisticChannel",booleanValue); 
        m_realisticChannel = booleanValue.Get();
        GlobalValue::GetValueByName("LEradius", doubleValue); 
        m_radius = doubleValue.Get();
        GlobalValue::GetValueByName("LEbuildings",booleanValue); 
        m_buildings = booleanValue.Get();
        GlobalValue::GetValueByName("LEemergency_time", uintegerValue); 
        m_emergency_time = uintegerValue.Get(); 
    }

    //--- Set the global variables from the configuration file  ---//
    void DynamicGateway::SetGlobalsFromConfig(){
        NS_LOG_FUNCTION("Setting Globals from Configuration."); 

        UintegerValue uintegerValue; 
        DoubleValue doubleValue; 
        StringValue stringValue; 
        BooleanValue booleanValue; 

        g_nNodes.SetValue(UintegerValue(m_nNodes));
        g_nRspu.SetValue(UintegerValue(m_nRspu));
        g_packetSize.SetValue(UintegerValue(m_packetSize));
        g_verbose.SetValue(BooleanValue(m_verbose));
        g_CsvFileName.SetValue(StringValue(m_CsvFileName));
        g_traceFile.SetValue(StringValue(m_traceFile));
        g_logFile.SetValue(StringValue(m_logFile));
        g_totalSimTime.SetValue(DoubleValue(m_TotalSimTime));
        g_realisticChannel.SetValue(BooleanValue(m_realisticChannel));
        g_buildings.SetValue(BooleanValue(m_buildings));
        g_radius.SetValue(DoubleValue(m_radius)); 
        g_emergency_time.SetValue(UintegerValue(m_emergency_time)); 
    }

    //--- Configure the channel to be used  ---//
    Ptr<LoraChannel> DynamicGateway::ConfigureChannel(){
            NS_LOG_FUNCTION("Configuring the Channel parameters(loss,delay).");
            Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel> ();
            loss->SetPathLossExponent (3.76);
            loss->SetReference (1, 7.7);

            if (m_realisticChannel){
                Ptr<CorrelatedShadowingPropagationLossModel> shadowing = CreateObject<CorrelatedShadowingPropagationLossModel> ();
                loss->SetNext (shadowing);
                Ptr<BuildingPenetrationLoss> buildingLoss = CreateObject<BuildingPenetrationLoss> ();
                shadowing->SetNext (buildingLoss);
            }

            Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel> ();
            return CreateObject<LoraChannel> (loss, delay);
    }

    void DynamicGateway::ConfigureHelpers(uint32_t state,NodeContainer nodes){
        NS_LOG_FUNCTION(state); 
        switch (state){
            case 0: //Initialization 
                lora_app.m_phyHelper = LoraPhyHelper();
                lora_app.m_phyHelper.SetChannel(lora_app.m_loraChannel);
                lora_app.m_macHelper = LorawanMacHelper();   
                lora_app.m_loraHelper = LoraHelper(); 
                lora_app.m_loraHelper.EnablePacketTracking(); 
                break;
            case 1: //Configure channels for Gateways. 
                lora_app.m_phyHelper.SetDeviceType(LoraPhyHelper::GW); 
                lora_app.m_macHelper.SetDeviceType(LorawanMacHelper::GW); 
                lora_app.m_macHelper.SetRegion(LorawanMacHelper::EU); 
                lora_app.m_loraHelper.Install(lora_app.m_phyHelper,lora_app.m_macHelper,nodes);
                break; 
            case 2: //Configure the initial vehicles 
                lora_app.m_phyHelper.SetDeviceType(LoraPhyHelper::ED);
                lora_app.m_macHelper.SetDeviceType(LorawanMacHelper::ED_C);
                lora_app.m_macHelper.SetAddressGenerator(m_addrGen);
                lora_app.m_macHelper.SetRegion(LorawanMacHelper::EU);
                lora_app.m_loraHelper.Install(lora_app.m_phyHelper, lora_app.m_macHelper, nodes);
                break; 
            case 4: //Only install helper onto the new nodes. 
                m_nwkId = m_nwkId +  lora_app.events;
                m_nwkAddr = m_nwkAddr + lora_app.events;  
                m_addrGen = CreateObject<LoraDeviceAddressGenerator> (m_nwkId,m_nwkAddr);
                lora_app.m_phyHelper.SetDeviceType(LoraPhyHelper::ED);
                lora_app.m_macHelper.SetDeviceType(LorawanMacHelper::ED_C);
                lora_app.m_macHelper.SetAddressGenerator(m_addrGen);
                lora_app.m_macHelper.SetRegion(LorawanMacHelper::EU);
                lora_app.m_loraHelper.Install(lora_app.m_phyHelper, lora_app.m_macHelper, nodes);
            default:
                break;
        }
    }

    NodeContainer DynamicGateway::CreateNodes(uint32_t state, bool traceMobility,int index_before,  uint32_t number_nodes, std::string tracefile,double radius){
        NS_LOG_FUNCTION(traceMobility << index_before << number_nodes << tracefile << radius);
        
        //Creation of new nodes. 
        NodeContainer new_vehicles; 
        new_vehicles.Create(number_nodes); 
        
        //CREATE MOBILITY FOR NODES BASED ON TRACE SOURCE FILE OR RANDOM UNIFORM POSITION ALLOCATOR. 
        if (traceMobility){ 
            //TRACE SOURCE
            Ns2MobilityHelper ns2 = Ns2MobilityHelper(tracefile);
            ns2.Install(new_vehicles.Begin(),new_vehicles.End()); 

        }else{
            //UNIFORMAL DISC ALLOCATOR
            MobilityHelper mobilityVehicles;
            mobilityVehicles.SetPositionAllocator ("ns3::UniformDiscPositionAllocator", "rho", DoubleValue(radius),
                                        "X", DoubleValue (0.0), "Y", DoubleValue (0.0));
            std::random_device rd1; 
            std::mt19937 gen1(rd1());
            std::uniform_real_distribution<double> distribution1(-50.0,50.0);
            for (uint32_t ii = 0; ii != new_vehicles.GetN(); ii++){
                Ptr<ConstantVelocityMobilityModel> vel_mob = CreateObject<ConstantVelocityMobilityModel>(); 
                double vel = distribution1(gen1);
                vel_mob -> SetVelocity(Vector(2.0 + vel,0.0,0.0));
                new_vehicles.Get(ii)-> AggregateObject(vel_mob); 
            }
            mobilityVehicles.Install(new_vehicles); 
        }
    
        //Install to the newly created ED the phy and mac helpers. 
        ConfigureHelpers(state,new_vehicles); 

        for(NodeContainer::Iterator jj = new_vehicles.Begin(); jj!=new_vehicles.End(); jj++){
            lora_app.vehicles_States.insert(pair<Ptr<Node>, std::string>((*jj),"RX_P")); //Ready to receive the broadcast message.                         
            Ptr<LorawanMac> edMac = (*jj)->GetDevice (0)->GetObject<LoraNetDevice> ()->GetMac ();
            Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = edMac->GetObject<ClassCEndDeviceLorawanMac>();
            LoraDeviceAddress address = edLorawanMac->GetDeviceAddress();
            Ptr<EndDeviceLoraPhy> edPhy = (*jj)->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<EndDeviceLoraPhy>(); 
            edLorawanMac->OpenSecondReceiveWindow(false);
            edLorawanMac->SetMType (LorawanMacHeader::UNCONFIRMED_DATA_UP); 

            lora_app.m_createdNodes.insert({(*jj), address}); 
            (*jj)->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->TraceConnectWithoutContext("ReceivedPacket",MakeCallback(&ns3::CorrectReceptionEDCallback));
        }

        //Creation of clusters to store up to 8 devices to communicate with the gateway directly. 
        NS_LOG_FUNCTION("Creation of Nodes was successfull.");
        return new_vehicles; 
    }

//--- Configure the initial state of the simulation. Starting Nodes for GW, NS, EDs. ---//
    void DynamicGateway::ConfigureInitialNodes(){
        NS_LOG_FUNCTION_NOARGS(); 

        ConfigureHelpers(0, m_networkServer);
  
        /*******************Creating the Network Server node************************/
        MobilityHelper mobilityNs;
        Ptr<ListPositionAllocator> positionAllocNs = CreateObject<ListPositionAllocator> ();
        positionAllocNs->Add (Vector (0.0,0.0,0.0));
        mobilityNs.SetPositionAllocator (positionAllocNs);
        mobilityNs.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
        m_networkServer.Create(1); 
        mobilityNs.Install(m_networkServer);
           
        /*******************Creating the RSPU nodes ************************/
        MobilityHelper mobilityGw;
        Ptr<ListPositionAllocator> positionAllocGw = CreateObject<ListPositionAllocator> ();
        //positionAllocGw->Add (Vector (245.20,869.94,10.0));
        //positionAllocGw->Add (Vector (490.35,671.70,10.0));
        positionAllocGw->Add (Vector (710.59,497.63,10.0));
        //positionAllocGw->Add (Vector (924.46,324.26,10.0));
        //positionAllocGw->Add (Vector (1132.64,157.29,10.0));
        mobilityGw.SetPositionAllocator (positionAllocGw);
        mobilityGw.SetMobilityModel ("ns3::ConstantPositionMobilityModel");

        lora_app.m_rspus.Create(m_nRspu);
        mobilityGw.Install(lora_app.m_rspus); 

        //Install the rspu to the helpers 
        ConfigureHelpers(1, lora_app.m_rspus);
        
        for(NodeContainer::Iterator jj = lora_app.m_rspus.Begin(); jj != lora_app.m_rspus.End(); jj++){
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("PhyRxBegin",MakeCallback(&ns3::PhyRxBegin));
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> GetObject<GatewayLoraPhy>() -> TraceConnectWithoutContext("OccupiedReceptionPaths",MakeCallback(&ns3::OccupiedReceptionPaths)); 
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("LostPacketBecauseInterference",MakeCallback(&ns3::OnReceiveInterferenceGW));
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(&ns3::OnReceivePacketGW));
            Ptr<GatewayLoraPhy> gwPhy = (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> GetObject<GatewayLoraPhy>();
            gwPhy->AddFrequency(868.1);
            gwPhy->AddFrequency(868.3);
        }

        /*******************Creating the vehicle nodes************************/
        m_addrGen = CreateObject<LoraDeviceAddressGenerator> (m_nwkId,m_nwkAddr);
        NodeContainer new_vehicles = CreateNodes(2,true, 0, m_initial_nodes,m_traceFile,m_radius);
        lora_app.m_vehicles.Add(new_vehicles); 
        lora_app.m_macHelper.SetSpreadingFactorsUp(lora_app.m_vehicles,lora_app.m_rspus,lora_app.m_loraChannel);

        /******************* Installing Devices to the network ************************/
        m_networkServerHelper.SetGateways(lora_app.m_rspus); 
        //m_networkServerHelper.EnableAdr(m_adr);
        ApplicationContainer apps = m_networkServerHelper.Install(m_networkServer); 
        lora_app.m_ns = DynamicCast<NetworkServer>(apps.Get(0)); 
        lora_app.m_forwarderHelper.Install(lora_app.m_rspus); 

        /******************* Creating the additional sensor node in the edge of the network *****************/        
        lora_app.sensorNode.Create(1);

        Simulator::Schedule(Seconds(1),&ns3::CreateSubsets); 


        //--- First Broadcast to get the initial nodes of the network ---//
        Simulator::Schedule(lora_app.Next_GW_transmission, &ns3::SendBroadcastGW, Create<Packet>(10),lora_app.m_rspus);
        std::random_device rd; 
        std::mt19937 gen(rd()); //Marsenne Twister engine
        double correctness = 200.0; 
        std::uniform_real_distribution<double> distribution(correctness, lora_app.Simulation);
        double diff = 200; 
        for(uint32_t emergency = 0 ; emergency < m_emergency_time; emergency++){
                double multitude = distribution(gen); 
                if(emergency>1){
                    std::uniform_real_distribution<double> newones(correctness, lora_app.Simulation);
                    double multitude = newones(gen);  
                }
                if(abs(multitude-correctness) > diff){
                    correctness = multitude; 
                    Simulator::Schedule(Seconds(multitude),&ns3::CreateEvent,Create<Packet>(10));
                }else{
                    continue; 
                }
        }
        
        NS_LOG_DEBUG("The Number of registered devices associated with the channel :: "<<lora_app.m_loraChannel->GetNDevices());
        NS_LOG_DEBUG("Successfully initialized starting network server, nodes and trace sources!");
    }

//--- Sets default attributes depending on the classes called by the program. ---//
    void DynamicGateway::ConfigureDefaults()
    {
        NS_LOG_FUNCTION_NOARGS(); 
    }

    //--- The Call to execute the simulation ---//
    void DynamicGateway::RunSimulation(){
        NS_LOG_FUNCTION_NOARGS(); 
        Run(); 
    }

    //--- The Execution of the simulation ---//
    void DynamicGateway::Run(){
        NS_LOG_FUNCTION_NOARGS(); 
        AnimationInterface anim("DNP_Animation.xml");//Perhaps when called after run it wont do anything 
        Simulator::Stop(Seconds(m_TotalSimTime)); 
        Simulator::Run(); 
        ProcessOutputs(); 
        Simulator::Destroy(); 
    }

    //--- Handles the creation of buildings to alter the channel parameters. Cannot be showcased in NetAnim ---//
    void DynamicGateway::HandleBuildings (){
        NS_LOG_FUNCTION("Creating Buildings for regular simulation. ");        
        double xLength = 130;
        double deltaX = 32;
        double yLength = 64;
        double deltaY = 17;
        int gridWidth = 2 * m_radius / (xLength + deltaX);
        int gridHeight = 2 * m_radius / (yLength + deltaY);

        if (m_realisticChannel == false){
            gridWidth = 0; 
            gridHeight = 0;
        }

        Ptr<GridBuildingAllocator> gridBuildingAllocator;
        gridBuildingAllocator = CreateObject<GridBuildingAllocator> ();
        gridBuildingAllocator->SetAttribute ("GridWidth", UintegerValue (gridWidth));
        gridBuildingAllocator->SetAttribute ("LengthX", DoubleValue (xLength));
        gridBuildingAllocator->SetAttribute ("LengthY", DoubleValue (yLength));
        gridBuildingAllocator->SetAttribute ("DeltaX", DoubleValue (deltaX));
        gridBuildingAllocator->SetAttribute ("DeltaY", DoubleValue (deltaY));
        gridBuildingAllocator->SetAttribute ("Height", DoubleValue (6));
        gridBuildingAllocator->SetBuildingAttribute ("NRoomsX", UintegerValue (2));
        gridBuildingAllocator->SetBuildingAttribute ("NRoomsY", UintegerValue (4));
        gridBuildingAllocator->SetBuildingAttribute ("NFloors", UintegerValue (2));
        gridBuildingAllocator->SetAttribute (
            "MinX", DoubleValue (-gridWidth * (xLength + deltaX) / 2 + deltaX / 2));
        gridBuildingAllocator->SetAttribute (
            "MinY", DoubleValue (-gridHeight * (yLength + deltaY) / 2 + deltaY / 2));
        BuildingContainer bContainer = gridBuildingAllocator->Create (gridWidth * gridHeight);
        
        BuildingsHelper::Install(lora_app.m_vehicles); 
        BuildingsHelper::Install(lora_app.m_rspus); 

        // Print the buildings
        if (m_verbose){
            std::ofstream myfile;
            myfile.open ("buildings.txt");
            std::vector<Ptr<Building>>::const_iterator it;
            int j = 1;
            for (it = bContainer.Begin (); it != bContainer.End (); ++it, ++j)
                {
                Box boundaries = (*it)->GetBoundaries ();
                myfile << "set object " << j << " rect from " << boundaries.xMin << "," << boundaries.yMin
                        << " to " << boundaries.xMax << "," << boundaries.yMax << std::endl;
                }
            myfile.close ();
        }
    }

    //--- The Callback for the Addition of Nodes in the Simulation, called multiple times ---//
    void DynamicGateway::ConfigureSimulation(void* classInstance){
        //if(!lora_app.succ_broadcast){Simulator::Schedule(Simulator::Now(), &ns3::SendBroadcastGW, Create<Packet>(10),lora_app.m_rspus); return; }
        lora_app.events++;
        NS_LOG_FUNCTION("Callback function for program reached successfully.");
        
        //if (!lora_app.m_createdNodes.empty()){lora_app.m_createdNodes.clear();} 
        DynamicGateway* project_pointer = static_cast<DynamicGateway*>(classInstance); 

        std::random_device rd; 
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> distribution(100,300);
        int multitude = distribution(gen);  

        /******************* Creating the new devices of the network ************************/
        NodeContainer new_vehicles = project_pointer->CreateNodes(4,false, project_pointer->m_initial_nodes,multitude,"",project_pointer->m_radius); 
        for(NodeContainer::Iterator vehicle = new_vehicles.Begin(); vehicle != new_vehicles.End(); vehicle++){
            LoraDeviceAddress address = (*vehicle)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>()->GetDeviceAddress(); 
            lora_app.m_createdNodes.insert({(*vehicle), address}); 
        }

        /******************* Installing the phy and mac layer helpers to the new devices ************************/
        lora_app.m_vehicles.Add(new_vehicles); 
        lora_app.m_macHelper.SetSpreadingFactorsUp(new_vehicles,lora_app.m_rspus,lora_app.m_loraChannel);

        Time diff = Seconds(0); 
        diff = Simulator::Now()-lora_app.broadcast_time; 
        if(diff>lora_app.Next_GW_transmission){
            Simulator::Schedule(Seconds(1), &ns3::SendBroadcastGW, Create<Packet>(10),lora_app.m_rspus);
        }else{
            Simulator::Schedule(lora_app.Next_GW_transmission-diff, &ns3::SendBroadcastGW, Create<Packet>(10), lora_app.m_rspus); 
        }
        NS_LOG_DEBUG("Created Nodes Successfully at time :: " << Simulator::Now());

    }

    //--- Process Outputs to Validate the Simulation ---//
    void DynamicGateway::ProcessOutputs(){
        NS_LOG_FUNCTION_NOARGS(); 
        LoraPacketTracker &tracker = lora_app.m_loraHelper.GetPacketTracker ();
        NS_LOG_INFO("Printing total sent MAC-layer packets and successful MAC-layer packets");
        NS_LOG_INFO(tracker.CountMacPacketsGlobally (Seconds (0), Seconds(m_TotalSimTime))); 
        if (m_log){
            NS_LOG_UNCOND("PacketsSent/Received => " << tracker.CountMacPacketsGlobally (Seconds (0), Seconds(m_TotalSimTime)) );
        }
        std::cout<<"THE TOTAL NUMBER OF NODES SUCCESSFULLY ADDED TO THE NETWORK IS :: "<<lora_app.m_ns->GetNetworkStatus()->CountEndDevices()<<std::endl; 
        std::cout<<"THE TOTAL NUMBER OF NODES THAT SHOULD HAVE BEEN ADDED TO THE NETWORK IS :: "<<lora_app.m_vehicles.GetN()<<std::endl;
        std::cout<<"THE TOTAL NUMBER OF BROADCASTED PACKETS :: "<<lora_app.count_norm_broadcast<<std::endl;
        std::cout<<"THE Current Status of the simulation scenario regarding events :: "<<lora_app.event_occurred<<std::endl;
        std::cout<<"The Total Number of lost packets during transmission from the EDs :: " << lora_app.count_lost_packets_Intf<<std::endl;
        std::cout<<"The Total Number of cancelled EDs transmissions due to DutyCycle :: " << lora_app.count_lost_packets_DC<<std::endl;
        std::cout<<"The Total Number of failed transmissions from the EDs :: " << lora_app.count_failed_packets<<std::endl;        
        std::cout<<"The Total Number of Devices in the Network when event occured :: " << lora_app.eme_nodes<<std::endl;
        std::cout<<"The Total Number of Devices that received the EM in the Network when event occured :: " << lora_app.count_emerg_devices<<std::endl;
        std::cout<<"The Total Number of emergencies detected ::  " << lora_app.count_emergencies<<std::endl;

        //lora_app.m_loraHelper.PrintPerformance(Seconds(0),Seconds(lora_app.Simulation)); 
        //lora_app.m_loraHelper.CountPhyPackets(Seconds(0),Seconds(lora_app.Simulation)); 
        for(NodeContainer::Iterator node = lora_app.m_vehicles.Begin();node != lora_app.m_vehicles.End();++node){
            Ptr<LorawanMac> mac = (*node)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac();
            Ptr<EndDeviceLorawanMac> edMac = mac->GetObject<EndDeviceLorawanMac>(); 
            LoraDeviceAddress address = edMac->GetDeviceAddress(); 
            Ptr<EndDeviceStatus> edStatus = CreateObject<EndDeviceStatus>(address, edMac); 
            //std::cout<<unsigned(mac->GetObject<ClassCEndDeviceLorawanMac>()->GetFirstReceiveWindowDataRate());
            //std::cout<<unsigned(edStatus->GetFirstReceiveWindowSpreadingFactor()); 
        }
        
        m_os.close(); //Close log file
    }

    //--- Write results into a CSV file for further analysis ---//
    void DynamicGateway::WriteCsvHeader(){
        NS_LOG_FUNCTION_NOARGS(); 
    }
}

int main (int argc, char* argv[]){
    DynamicGateway experiment; 
    experiment.Simulate (argc, argv);
    uint32_t additional_nodes_creation = 3; 
    std::random_device rd; 
    std::mt19937 gen(rd()); //Marsenne Twister engine
    double correctness = 500.0; 
    std::uniform_real_distribution<double> distribution(correctness, lora_app.Simulation);
    double diff = 600; 
    for(uint32_t emergency = 0 ; emergency < additional_nodes_creation; emergency++){
        double multitude = distribution(gen); 
        if(emergency>1){
            std::uniform_real_distribution<double> newones(correctness, lora_app.Simulation);
            double multitude = newones(gen);  
        }
        if(abs(multitude-correctness) > diff){
            correctness = multitude; 
            //Simulator::Schedule(Seconds(multitude),&DynamicGateway::ConfigureSimulation, &experiment);
        }else{
            continue; 
        }
    }
   
    //Simulator::Schedule(Seconds(multitude), &DynamicGateway::ConfigureSimulation, &experiment);
    //Simulator::Schedule(Seconds(1000), &DynamicGateway::ConfigureSimulation, &experiment);
    experiment.RunSimulation(); 
    return 0;
}