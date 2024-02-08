//This will be the file to be used for the demo in the EDGEAI Presentation
//The Goal is to have multiple moving Nodes be broadcasted a message from the network
//which should happen at random times. If they receive it correctly then they must sent a reply. 
//After all applicable nodes have succesfully sent a reply then they all send the message. 
//We can calculate the number of packets that the gateway can receive in the duration of an hour. 
//If that is surpassed then the end devices will retransmit their message again. 
//In addition we want to be able to add moving devices to the program in patches while it is 
//running. *The animation will only be available for the beginning devices. 
//Lastly we should astride to have multiple gateways not only one. 

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

//---Basic NS3 imports---//
#include "ns3/node-container.h"
#include "ns3/log.h"
#include "ns3/command-line.h"
#include "ns3/config-store-module.h" 
#include "ns3/core-module.h"
#include "ns3/callback.h"
#include "ns3/traced-callback.h"
#include "ns3/lora-channel.h"
#include "ns3/correlated-shadowing-propagation-loss-model.h"
#include "ns3/mobility-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/position-allocator.h"
#include "ns3/constant-position-mobility-model.h"

//---Channel Helpers based on Layer---// 
#include "ns3/lora-phy-helper.h"
#include "ns3/lorawan-mac-helper.h" 
#include "ns3/lora-helper.h"

#include "ns3/forwarder-helper.h" 
#include "ns3/network-server-helper.h"
#include "ns3/network-module.h"
 
//---Application Helpers---//
#include "ns3/periodic-sender-helper.h" 
#include "ns3/one-shot-sender-helper.h"

//---Buildings Helpers---//
#include "ns3/building-penetration-loss.h"
#include "ns3/building-allocator.h" 
#include "ns3/buildings-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-helper.h"


//--- Plots ---// 
#include "ns3/gnuplot.h"
#include "ns3/histogram.h"
// #include "gnuplot-iostream.h"
//---Basic C++ imports---//
#include <random>
#include <fstream>
#include <iostream> 
#include <vector>
#include <ctime>
#include <cstdlib>
#include <map>
#include <boost/bimap.hpp>
#include <tuple> 
#include <queue>
#include <set> 
#include <chrono> 
#include <algorithm> 


using namespace ns3;
using namespace lorawan; 
using namespace std; 
using namespace boost::bimaps; 

typedef bimap<Ptr<Node>,LoraDeviceAddress> Bimap;

NS_LOG_COMPONENT_DEFINE("DynamicTransmissions");

std::vector<double> PDR_GW; 
std::vector<double> PDR_ED; 
std::vector<double> PER_ED; 
std::vector<double> PER_GW; 
std::vector<double> PLR_GW;
std::vector<double> PLR_ED; 
std::vector<int> Count_Acc_EDs;
std::vector<int> Count_Endangered;  
std::vector<Time> OnAir; 
int nSims;  
std::vector<std::tuple<uint32_t,uint32_t>> Regard_EM; 
std::vector<std::tuple<int,int,double, double>> SentByEDs; 
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
        
        //--- Necessary tables to manage the devices and the scheduling of pkts ---//
        Bimap m_createdNodes; //Holds the created nodes and deletes each node that correctly is added to the net. 
        std::vector<Ptr<Node>> excluded_nodes; // the devices that are outside the pre defined range from the GW at a specific time.  
        std::vector<std::set<Ptr<Node>>> Subsets; // Create subsets of devices based on distance from the GW. 
        std::set<Ptr<Node>> remainingNodes; //the nodes remaining to receive an ACK message. 
        std::map<LoraDeviceAddress, Ptr<EndDeviceStatus>> m_endDeviceStatuses; //Info about the devices that have successfully received a br package. 
        std::map<Address, Ptr<GatewayStatus>> m_gatewayStatuses; //Info about the GW that will br the pkt. TODO: Have multiple gw used in the program. 
        std::set<LoraDeviceAddress> ACK_Queue; //Holds the remaining addresses to receive the ACK 
        std::set<LoraDeviceAddress> EmergencyQueue;
        std::map<LoraDeviceAddress, std::string> vehicles_States;
        std::map<LoraDeviceAddress, std::string> prev_veh_States;
        std::vector<Address> gwAddresses_vec; 
        std::queue<uint32_t> phy_end_reception_count; 
        std::queue<bool> ignore_broadcast_nodes;
        std::queue<Time> gw_next_broad; 
        std::set<LoraDeviceAddress> ACK_Devices; 
        
        //--- Result Variables ---//
        //(the below vectors hold info about the event id , the number of devices either in danger or created, and the  number of ED received the em pkt or that successfully were added to the network. )
        std::vector<std::tuple<uint32_t,uint32_t, uint32_t,Time>> EmergencyVector; 
        std::vector<std::tuple<uint32_t,uint32_t, uint32_t>> NodeCreationVector; //eventID, number of nodes created, number of nodes that were applicable, number of nodes that were accepted by net.  
        std::vector<uint8_t> packetsSent; //These are used for later metrics 
        std::vector<uint8_t> packetsReceived;
        std::vector<tuple<uint32_t, double, Time>> PDR;
        std::tuple<uint32_t, double, Time> PER;  
        
        //--- Basic Simulation Configuration ---///
        double SimulationTime; //Time of Simulation. 
        int maxReceptionPaths; //number of maximum reception paths that the GW uses. (8) 
        Time broadcast_time; // Get the time that a br is executed. 
        Time emergency_delay; 
        uint32_t applicable_nodes; // Number to hold the applicable devices as they are formed by the Subsets. 
        uint32_t stationary_nodes; 
        bool event_occurred;         
        int m_flag; //Flag to stop to access the emergency packet send by the sensor. 
        LoraDeviceAddress sensor_Address; 
        bool exit_clause; //Break the recursion in the case program tries over and over to broadcast. 
        bool creation_flag; //Inform the program if the creation of nodes has happend
        int emergency_event_id; 
        uint32_t gw_systemId; 
        uint32_t addition_event_id;
        uint32_t lost_packets_due_Intf; 
        uint32_t lost_packets_due_DutyC; 
        uint32_t NS_success_broadcasts; 
        uint32_t failed_transmissions_EDs;
        uint32_t Num_EDs_received_EM;
        uint32_t ED_in_danger_Emergency; 
        uint32_t EDs_in_NS_at_Emergency; 
        double finished_gw_reception;
        double Correct_GW_DL_transmissions; 
        double Correct_ED_DL_receptions; 
        double Correct_ED_ACK_receptions; 
        double Correct_ACK_GW_transmissions; 
        double Correct_ED_UL_transmissions; 
        double Correct_GW_UL_receptions; 
        double Incorrect_ACK; 
        uint32_t Num_EDs_created;
        uint32_t lost_ACK_pkts; 
        uint32_t accepted_devices;
        double emergency_time; 
        //--- Variables to hold the nodes ---//
        NodeContainer m_rspus; 
        NodeContainer m_vehicles;
        NodeContainer sensorNode; 
        Ptr<NetworkServer> m_ns; 

        //--- Application and packets ---//
        PeriodicSenderHelper perShot; 
        ApplicationContainer app;

        //--- Helpers ---//
        Ptr<LoraChannel> m_loraChannel; 
        Ptr<LoraChannel> DedicatedChannel; 
        LoraPhyHelper m_phyHelper; 
        LoraPhyHelper m_critical_phyHelper;
        LorawanMacHelper m_macHelper; 
        LorawanMacHelper m_critical_macHelper; 
        LoraHelper m_loraHelper; 
        LoraHelper m_critical_loraHelper;
        ForwarderHelper m_forwarderHelper; 

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
        :SimulationTime(3600.0), 
         maxReceptionPaths(8), 
         broadcast_time(Seconds(0)),
         emergency_delay(Seconds(150)), 
         applicable_nodes(0), 
         stationary_nodes(2), 
         event_occurred(false), 
         m_flag(1000),
         exit_clause(false),
         creation_flag(false),
         emergency_event_id(0), 
         addition_event_id(0),
         lost_packets_due_Intf(0), 
         lost_packets_due_DutyC(0), 
         NS_success_broadcasts(0), 
         failed_transmissions_EDs(0), 
         Num_EDs_received_EM(0), 
         ED_in_danger_Emergency(0), 
         EDs_in_NS_at_Emergency(0), 
         finished_gw_reception(0.0),
         Correct_GW_DL_transmissions(0), 
         Correct_ED_DL_receptions(0), 
         Correct_ED_ACK_receptions(0),
         Correct_ACK_GW_transmissions(0), 
         Correct_ED_UL_transmissions(0), 
         Correct_GW_UL_receptions(0), 
         Incorrect_ACK(0), 
         Num_EDs_created(0),
         lost_ACK_pkts(0),  
         accepted_devices(0),
         emergency_time(0)
    {
        packetsSent = std::vector<uint8_t >(6, 0); 
        packetsReceived = std::vector<uint8_t >(6, 0); 
        m_forwarderHelper = ForwarderHelper();
        app = ApplicationContainer(); 
        perShot = PeriodicSenderHelper(); 
    }

    //--- Instance of a LoRa App class to handle the parameters during execution. ---//
    //LoraApp lora_app = LoraApp();
    Ptr<LoraApp> lora_app; 

    LoraApp::~LoraApp()
    {
    }

    void LoraApp::Simulate(int argc, char** argv){
        lora_app = CreateObject<LoraApp>(); 
        NS_LOG_FUNCTION("Starting Simulation Configuration " << argc << " " << argv);
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

    //------------------------------------- --------------------------------------//
    //--- Callback Functions Used to Handle Transmissions for both GW and EDs  ---//
    //------------------------------------- --------------------------------------//
   
    void GwSentPacketCallback(Ptr<Packet const> packet){
        //Get the packet GW transmitted 
        //NS_LOG_FUNCTION_NOARGS(); 
        LorawanMacHeader mHdr; 
        LoraFrameHeader fHdr; 
        Ptr<Packet> myPacket = packet->Copy();
        myPacket->RemoveHeader(mHdr);
        myPacket->RemoveHeader(fHdr);
        LoraTag tag;
        packet->PeekPacketTag(tag);
        LoraDeviceAddress address = fHdr.GetAddress();
        if(address.IsBroadcast() && !lora_app->event_occurred){ 
            for(uint32_t node = 0; node <lora_app->gwAddresses_vec.size(); node++){
                lora_app->gw_next_broad.push(lora_app->m_rspus.Get(node)->GetDevice(0)->GetObject<LoraNetDevice>()-> GetMac()->GetObject<GatewayLorawanMac>()-> GetWaitingTime(869.525)); 
            }
            NS_LOG_DEBUG("Echo broadcast transmission is sent...");
            lora_app->NS_success_broadcasts++;

        }else if(address.IsBroadcast() && lora_app->event_occurred){
            //This is an emergency Broadcast event.
            NS_LOG_DEBUG("Emergency broadcast transmission is sent...");
            lora_app->NS_success_broadcasts++;

            for(uint32_t node = 0; node <lora_app->gwAddresses_vec.size(); node++){
                lora_app->gw_next_broad.push(lora_app->m_rspus.Get(node)->GetDevice(0)->GetObject<LoraNetDevice>()-> GetMac()->GetObject<GatewayLorawanMac>()-> GetWaitingTime(869.525)); 
            }

            std::random_device rd; 
            std::mt19937 gen(rd()); 
            std::uniform_real_distribution<double>distribution(60, 300); 
            double duration = distribution(gen);
            Simulator::Schedule(Seconds(duration),[](){
                lora_app->event_occurred = false;
                tuple<int,uint32_t,uint32_t,Time> tmp; 
                std::cout<<lora_app->ED_in_danger_Emergency<< " & " << lora_app->Num_EDs_received_EM<<std::endl;
                tmp = make_tuple(lora_app->emergency_event_id, lora_app->ED_in_danger_Emergency,lora_app->Num_EDs_received_EM,(Simulator::Now())); 
                bool duplicated = false;
                for (const auto& tuple: lora_app->EmergencyVector){
                    if (std::get<0>(tuple) == lora_app->emergency_event_id)
                    {
                        duplicated = true; 
                        break; 
                    }
                }
                
                if (!duplicated){
                    lora_app->EmergencyVector.push_back(tmp);
                }   
                lora_app->emergency_time = 0;
            });

        }else{
            //NS_LOG_DEBUG("ACK  transmission is sent...");
            lora_app->Correct_ACK_GW_transmissions++; 
        }
        lora_app->Correct_GW_DL_transmissions++; 
        // LoraTxParameters txParams;
        // txParams.sf = tag.GetSpreadingFactor();
        // Time onair = lora_app->m_rspus.Get(0)->GetDevice(0)->GetObject<LoraNetDevice> () -> GetPhy() -> GetOnAirTime(myPacket,txParams); 
        // OnAir.push_back(onair);
        //NS_LOG_FUNCTION_NOARGS(); 
        return;
 
    }

    //--- The Broadcast message of the GW  ---//from 50 to 200 bytes,
    Ptr<Packet> CreateBroadcastPacket(Ptr<Packet> data){ 
        NS_LOG_FUNCTION(data);
        LoraDeviceAddress address;
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        LoraTag tag;
        address.SetNwkID (0x7F);
        address.SetNwkAddr (0x1FFFFFF);
        mHdr.SetMType (LorawanMacHeader::UNCONFIRMED_DATA_DOWN);
        mHdr.SetMajor (1);
        fHdr.SetAsDownlink ();
        fHdr.SetFPort (1); 
        fHdr.SetAddress (address);
        fHdr.SetAdr (true);
        fHdr.SetAck (false);
        fHdr.SetAdrAckReq (0);
        fHdr.SetFCnt (0);
        fHdr.SetFPending (false);
        tag.SetFrequency (869.525);
        tag.SetSpreadingFactor(12);
        tag.SetDataRate (0);
        Ptr<Packet> packet = data->Copy();
        packet->AddHeader (fHdr);
        packet->AddHeader (mHdr);
        packet->AddPacketTag (tag);
        NS_LOG_DEBUG("Broadcast packet created succesfully with size [ " << packet->GetSize() << " bytes].");
        return packet; 
    }

     void CreateSubsets(){
        NS_LOG_FUNCTION_NOARGS(); 
        if(lora_app->excluded_nodes.size() != 0){lora_app->excluded_nodes.clear();} //Create a new excluded list due to movement and new addition of nodes. 
        lora_app->applicable_nodes = 0; //Value changes just like excluded_nodes. 
        //Function to create the subsets based on distance. 
        std::vector<std::set<Ptr<Node>>> subsets; 
        std::set<Ptr<Node>> set1; 
        subsets.push_back(set1);
        subsets.push_back(set1);
        subsets.push_back(set1);
        subsets.push_back(set1);
        subsets.push_back(set1);
        Ptr<MobilityModel> gwMob = lora_app->m_rspus.Get(0)->GetObject<MobilityModel>(); // Get Position of the GW. 
        for(auto edNode = lora_app->m_vehicles.Begin(); edNode != lora_app->m_vehicles.End() ; edNode++){

            double distance = gwMob->GetDistanceFrom((*edNode)->GetObject<MobilityModel>()); //Get distance between ED and GW after broadcast. 
            Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = (*edNode)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac() ->GetObject<ClassCEndDeviceLorawanMac>();
            LoraDeviceAddress edAddress = (*edNode)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>()->GetDeviceAddress();    
            auto addr_pos = lora_app->ACK_Queue.find(edAddress);
            auto ack_pos = lora_app->ACK_Devices.find(edAddress);
            if (addr_pos != lora_app->ACK_Queue.end()){
                //Device has sent pkt awaits for ACK. 
                continue; 
            }else if(ack_pos != lora_app->ACK_Devices.end()){
                continue; 
            }
            
            if(distance < 1000.0){
                (subsets[0]).insert((*edNode));
                lora_app->applicable_nodes++; 
                // edLorawanMac->SetDataRateAdaptation(true);
                // edLorawanMac->SetDataRate(5); 
                // edLorawanMac-> GetSfFromDataRate(5);
            }else if (distance >= 1000.0 && distance < 2400.0){
                (subsets[1]).insert((*edNode));
                lora_app->applicable_nodes++; 
                // edLorawanMac->SetDataRateAdaptation(true);
                // edLorawanMac->SetDataRate(4);
                // edLorawanMac-> GetSfFromDataRate(4);
            }else if (distance >= 2400.0 && distance < 3800.0){
                (subsets[2]).insert({*edNode});
                lora_app->applicable_nodes++; 
                // edLorawanMac->SetDataRateAdaptation(true);
                // edLorawanMac->SetDataRate(3);
                // edLorawanMac-> GetSfFromDataRate(3);
            }else if (distance >= 3800.0 && distance < 5200.0){
                (subsets[3]).insert((*edNode));
                lora_app->applicable_nodes++; 
                // edLorawanMac->SetDataRateAdaptation(true);
                // edLorawanMac->SetDataRate(2);
                // edLorawanMac-> GetSfFromDataRate(2);
            }else if (distance >= 5200.0 && distance < 6400.0){
                (subsets[4]).insert((*edNode));  
                lora_app->applicable_nodes++;     
                // edLorawanMac->SetDataRateAdaptation(true);
                // edLorawanMac->SetDataRate(0); 
                // edLorawanMac-> GetSfFromDataRate(0);
            }else{
                lora_app->excluded_nodes.push_back(*edNode); 
            }
            
            lora_app->remainingNodes.insert(*edNode);
        }
        lora_app->m_macHelper.SetSpreadingFactorsUp(lora_app->m_vehicles,lora_app->m_rspus,lora_app->m_loraChannel);
        lora_app->Subsets = subsets;  

        NS_LOG_DEBUG("Number of devices that are applicable to communication due to range restrictions = "
                     << lora_app->applicable_nodes
                     << " at time : "
                     << Simulator::Now());
        NS_LOG_DEBUG("Number of Nodes awaiting for their ACK -> " << lora_app->remainingNodes.size()); 
        
        if (lora_app->creation_flag){
            tuple<uint32_t,uint32_t,uint32_t> tmp; 
            if(!lora_app->NodeCreationVector.empty()){
                auto previous = lora_app->NodeCreationVector.rbegin(); 
                tmp = make_tuple(lora_app->addition_event_id, lora_app->m_vehicles.GetN()-get<1>(*previous),lora_app->applicable_nodes); 
            }else{
                tmp = make_tuple(lora_app->addition_event_id, lora_app->m_vehicles.GetN(),lora_app->applicable_nodes); 
            }
            lora_app->NodeCreationVector.push_back(tmp);
            lora_app->creation_flag = false; 
        }
        //NS_LOG_FUNCTION_NOARGS(); 
    }

    //--- This is necessary only in the case that we want to showcase a GW broadcasting a message by its own. ---///
    std::map<Address,Ptr<NetDevice>> AvailableGWToSend(NodeContainer gateway, LoraDeviceAddress address, int window,Ptr<Packet> packet){
        //NS_LOG_FUNCTION_NOARGS(); 
        //This function is usefull for getting the real address of the gateway and its availability. 
        if (address.IsBroadcast()){
            lora_app->gwAddresses_vec.clear();
            //The message is A broadcast transmission. 
            std::map<Address,Ptr<NetDevice>> gwAddresses;
            for(NodeContainer::Iterator jj = gateway.Begin(); jj != gateway.End(); jj++){
                Ptr<Node> gwNode = (*jj);
                Ptr<NetDevice> netDevice = gwNode->GetDevice(1); 
                Ptr<PointToPointNetDevice> p2pNetDevice; 
                for (uint32_t i = 0; i < gwNode->GetNDevices(); i++){
                    p2pNetDevice = gwNode->GetDevice (i)->GetObject<PointToPointNetDevice> ();
                    if (p2pNetDevice != 0){
                        break;
                    }
                }  

                Ptr<GatewayLorawanMac> gwMac = netDevice->GetObject<LoraNetDevice>()->GetMac()->GetObject<GatewayLorawanMac>(); 
                NS_ASSERT(gwMac!=0); 

                Address gwAddress = p2pNetDevice->GetAddress();
                Ptr<GatewayStatus> gwStatus = Create<GatewayStatus>(gwAddress, netDevice, gwMac); 

                if((gwStatus->IsAvailableForTransmission(869.525))){
                    lora_app->m_gatewayStatuses.insert(pair<Address,Ptr<GatewayStatus>>(gwAddress,gwStatus)); 
                    if(lora_app->gwAddresses_vec.size() != 0){lora_app->gwAddresses_vec.clear();} 
                    lora_app->gwAddresses_vec.push_back(gwAddress); 
                    gwAddresses.insert(pair<Address,Ptr<NetDevice>>(gwAddress, netDevice));
                    NS_LOG_DEBUG("The Address of the Gateway to be used is :: " << gwAddress); 
                    if(lora_app->exit_clause){
                        Ptr<Packet> pkt = CreateBroadcastPacket(packet);
                        netDevice->Send(pkt,gwAddress,0x0800);
                        CreateSubsets(); 
                        Time waitTime = gwMac -> GetWaitingTime(869.525);
                        lora_app->gw_next_broad.push(waitTime); 

                        gwMac -> TxFinished(packet);
                        lora_app->exit_clause = false;  
                    }
                }else{
                    Simulator::Schedule(Seconds(5),[gateway,packet](){
                        AvailableGWToSend(gateway,LoraDeviceAddress(0xFF, 0xFFFFFFFF),2,packet);
                        lora_app->exit_clause = true;     
                    });    
                }
            }
            return gwAddresses;

        }else {
            //The message is a unicast transmission. TODO: Have the ns or the GW successfully send dedicated messages to the end devices. 
            Ptr<EndDeviceStatus> edStatus = lora_app->m_endDeviceStatuses.at(address); 
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
                bool isAvailable = lora_app->m_gatewayStatuses.find(it->second)->second -> IsAvailableForTransmission(replyFrequency); 
                if(isAvailable){
                    bestGWAddress.insert(pair<Address,Ptr<NetDevice>>((it->second),(lora_app->m_gatewayStatuses.find(it->second)->second ->GetNetDevice())));
                    break; 
                }
            }
            return bestGWAddress;
        }
        //NS_LOG_FUNCTION_NOARGS(); 
    }

    //--- Callback to check if the broadcastted packet reached the phy layer ---//
    void GWPhyRxBeginCallback(Ptr<Packet const> packet){
        //NS_LOG_FUNCTION_NOARGS();
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        Ptr<Packet> myPacket = packet->Copy();
        myPacket->RemoveHeader(mHdr);
        myPacket->RemoveHeader(fHdr);
        //NS_LOG_FUNCTION_NOARGS();
    }

    //--- Have the Network Server send the broadcast emergency packet ---//
    void NSSendPacketCallback(Ptr<NetworkServer> server, Ptr<Packet> pkt, LoraDeviceAddress endDevice) {
        //NS_LOG_FUNCTION_NOARGS(); 
        if(lora_app->event_occurred){
            NS_LOG_DEBUG("Emergency Broadcast Transmission Enabled."); 
            Ptr<NetworkStatus> net_status = server->GetNetworkStatus(); 
        
            std::map<Address,Ptr<NetDevice>> gwAddresses = AvailableGWToSend(lora_app->m_rspus, LoraDeviceAddress(0xFF, 0xFFFFFFFF),2,pkt); 
            Ptr<Packet> packet = CreateBroadcastPacket(pkt);
            if(!gwAddresses.empty()){
                for(auto& gwaddress : gwAddresses){
                    (gwaddress.second)->Send(packet,gwaddress.first,0x0800);
                    Ptr<GatewayLorawanMac> gwMac = (gwaddress.second)->GetObject<LoraNetDevice>()->GetMac()->GetObject<GatewayLorawanMac>(); 
                    Time waitTime = gwMac -> GetWaitingTime(869.525);
                    lora_app->gw_next_broad.push(waitTime); 
                    gwMac -> TxFinished(packet);
                }
            }
            lora_app->broadcast_time = Simulator::Now();

        }else{
            std::map<Address,Ptr<NetDevice>> gwAddresses = AvailableGWToSend(lora_app->m_rspus, LoraDeviceAddress(0xFF, 0xFFFFFFFF),2,pkt); 
            server->Send(pkt, endDevice,2);
            if(!gwAddresses.empty()){
                for(auto& gwaddress : gwAddresses){
                    Ptr<GatewayLorawanMac> gwMac = (gwaddress.second)->GetObject<LoraNetDevice>()->GetMac()->GetObject<GatewayLorawanMac>(); 
                    Time waitTime = gwMac -> GetWaitingTime(869.525);
                    lora_app->gw_next_broad.push(waitTime); 
                    gwMac -> TxFinished(pkt);
                }
            }
            lora_app->broadcast_time = Simulator::Now(); 
            
        }
        //NS_LOG_FUNCTION_NOARGS(); 
    }

    //--- Reception of Packets for GWs ---//
    void GWReceptioOfReplyPacket (Ptr<Packet const> packet, uint32_t systemId){
        //Get the reply packet to register the address of the End Device.
        //NS_LOG_FUNCTION_NOARGS(); 
        // std::cout<<"In GWReceptioOfReplyPacket"<<std::endl;
        lora_app->gw_systemId = systemId; 
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        fHdr.SetAsUplink(); 
        Ptr<Packet> myPacket = packet->Copy();
        myPacket->RemoveHeader(mHdr);
        myPacket->RemoveHeader(fHdr);
        LoraDeviceAddress address = fHdr.GetAddress (); 
        LoraTag tag;
        packet->PeekPacketTag(tag);
        lora_app->packetsReceived.at(tag.GetSpreadingFactor()-7)++; 
        lora_app->Correct_GW_UL_receptions++;
        uint8_t tmp = tag.GetReceivePower();
        if (tmp != 0){
            tuple<int,int,double,double> tmp_tuple; 
            tmp_tuple = make_tuple(unsigned(tag.GetSpreadingFactor()),unsigned(tag.GetDataRate()),(tag.GetReceivePower()),tag.GetFrequency());
            SentByEDs.push_back(tmp_tuple);
            // std::cout<<"Sent with SF --->> " << unsigned(tag.GetSpreadingFactor()) << std::endl;
            // std::cout<<"Sent with DataRate --->> " << unsigned(tag.GetDataRate()) << std::endl;
            // std::cout<<"Sent TX Power --->> " << (tag.GetReceivePower()) << std::endl;
            // std::cout<<"Sent In frequency --->> " << tag.GetFrequency() << std::endl;
            lora_app->Correct_ED_UL_transmissions++;

        }
        // LoraTxParameters txParams; 
        // txParams.sf = tag.GetSpreadingFactor(); 
        // Get The time of Air for the responses of the Eds. 
        // Time onair = lora_app->m_rspus.Get(0)->GetDevice(0)->GetObject<LoraNetDevice> () -> GetPhy() -> GetOnAirTime(myPacket,txParams); 
        // OnAir.push_back(onair); 

        if(lora_app->event_occurred && lora_app->m_flag == 1000){ 
            if(address == lora_app->sensor_Address){
                NS_LOG_DEBUG("The sensor succesfully informed the GW at Frequency [ " << tag.GetFrequency() << " MHz]" );
                lora_app->m_flag = 0; 
                Time diff = Simulator::Now() - lora_app->broadcast_time; 
                lora_app->m_ns -> AddNode(lora_app->sensorNode.Get(0)); //Πρόσθεσε τον κόμβο στο σύστημα γιατί αλλιώς απορρίπτεται η επικοινωνία και ρίχνει το πρόγραμμα. 
                Ptr<PeriodicSender> app = lora_app->sensorNode.Get(0)->GetApplication(0)->GetObject<PeriodicSender>(); 
                app->StopApplication(); //Σταμάτησε τον αισθητήρα από το να συνεχίσει να στέλνει. 
                Time next_broad = lora_app->gw_next_broad.front(); 
                lora_app->gw_next_broad.pop(); 

                if(diff > next_broad){
                    Simulator::ScheduleNow(&ns3::NSSendPacketCallback,lora_app->m_ns, Create<Packet>(10),address); 
                }else if(diff < next_broad){
                    Simulator::Schedule(next_broad, &ns3::NSSendPacketCallback,lora_app->m_ns, Create<Packet>(10),address);
                }

                std::cout<<"Emergency Signal detected by emergency Sensor at time "<<Simulator::Now().GetSeconds()<<std::endl;
                return;
            }   
        }

        /**********CHECK IF GW RECEIVED PKT FROM DEVICE THAT IS ALREADY ACCEPTED IN THE NETWORK*************/
        Ptr<NetworkStatus> net_status = lora_app->m_ns->GetNetworkStatus(); 
        std::map<LoraDeviceAddress, Ptr<EndDeviceStatus>> endDeviceStatuses = net_status->m_endDeviceStatuses;
        if(endDeviceStatuses.find(address)==endDeviceStatuses.end()){
            //If device is not registered in the network
            /*********CHECKING FIRST RECEPTION TO GRANT ACCESS TO THE NETWORK**************/
            Bimap::right_const_iterator it = lora_app->m_createdNodes.right.find(address);
            auto add_pos = lora_app->m_endDeviceStatuses.find(address);
            if (add_pos != lora_app->m_endDeviceStatuses.end()  &&  it != lora_app->m_createdNodes.right.end()){
                //If device has accepted an echo broadcast and it was just created in the network. 
                /***********IF THE DEVICE HAS SENT ONCE && IT IS THE FIRST TIME WE ARE RECEIVING THEN ADD DEVICE TO NETWORK***********/
                Ptr<EndDeviceLorawanMac> tobeAdded = lora_app->m_endDeviceStatuses[address] -> GetMac();
                lora_app->accepted_devices++;
                lora_app->m_ns -> GetNetworkStatus() ->AddNode(tobeAdded);
                lora_app->m_createdNodes.right.erase(it->first); 
                lora_app->ACK_Queue.insert(address);
                if(net_status ->CountEndDevices() == static_cast<int>(lora_app->applicable_nodes)){
                    lora_app->finished_gw_reception = Simulator::Now().GetSeconds();
                }        
            }
        }else{
            //std::cout<<"R"<<std::endl;
            if(lora_app->ACK_Devices.find(address)!=lora_app->ACK_Devices.end()){
                Ptr<EndDeviceStatus> status = endDeviceStatuses[address];
                Ptr<EndDeviceLorawanMac> edmac = status->GetMac(); 
                Ptr<Packet> replypacket = status -> GetCompleteReplyPacket(); 
                Ptr<ClassCEndDeviceLorawanMac> classC = edmac->GetObject<ClassCEndDeviceLorawanMac>(); 
                // Ptr<LoraPhy> phy = edmac->GetPhy(); 
                // if (phy -> GetObject<EndDeviceLoraPhy>() -> IsTransmitting()){
                //     classC-> postponeTransmission(lora_app->emergency_delay,replypacket);
                //     phy -> GetObject<EndDeviceLoraPhy>() -> SwitchToStandby(); 
                // }

                Ptr<NetDevice> netDevice = edmac->GetDevice();
                Ptr<Node> ed = netDevice -> GetNode(); 
                Ptr<PeriodicSender> app = ed->GetApplication(1)->GetObject<PeriodicSender>();
                app->StopApplication(); 
                // Simulator::Schedule(Seconds(60),[classC](){
                //     classC -> OpenSecondReceiveWindow(false);
                // });
                lora_app->vehicles_States[address] = "RX_P";    

            }
            // }else if (endDeviceStatuses.find(address)!=endDeviceStatuses.end() && lora_app->ACK_Devices.find(address)==lora_app->ACK_Devices.end()){
            //     // NS_LOG_FUNCTION("The device in here is not supported yet ?????? ");
            //     // Ptr<EndDeviceStatus> status = endDeviceStatuses[address];
            //     // Ptr<EndDeviceLorawanMac> edmac = status->GetMac(); 
            //     // Ptr<NetDevice> netDevice = edmac->GetDevice();
            //     // Ptr<Node> ed = netDevice -> GetNode(); 
            //     // Ptr<PeriodicSender> app = ed->GetApplication(0)->GetObject<PeriodicSender>();
            //     // app->StopApplication(); 
            //     // std::cout<<"Gotten the end device status for Address :: " << address << std::endl;
            //     // Ptr<ClassCEndDeviceLorawanMac> classC = edmac->GetObject<ClassCEndDeviceLorawanMac>(); 
            //     // classC -> OpenSecondReceiveWindow(false);
            //     // lora_app->vehicles_States[address] = "RX_P"; 
            //     // std::cout<<"Gotten the end device status for Address :: " << address << std::endl;
                
            // }
            return; 
        }
        //NS_LOG_FUNCTION_NOARGS(); 
    }

    //--- Reception of Packets to the GW Is Interrupted by interference ---//
    void OnReceiveInterferenceGWCallback(Ptr<Packet const> packet, uint32_t systemId){
        lora_app->lost_packets_due_Intf++; 
    }

    //--- Transmission of Packets cancelled due to Duty cycle of the EDs ---//
    void EDSentRejectionCallback(Ptr<Packet const> packet){
        lora_app->lost_packets_due_DutyC++;
    }

    //--- Callback to determine wheather a packet has been sent to the mac layer ---//
    void FinishedTransmissionCallback(uint8_t transmissions, bool successful, Time firstAttempt, Ptr<Packet> packet){    
        if(!successful){
            lora_app->failed_transmissions_EDs++;
        }
        //NS_LOG_FUNCTION_NOARGS();
    }

    //--- Successfull transmission of packet from the ED towards the GW ---//
    void EdDeviceSenttoPhyCallback (Ptr<const Packet> packet, uint32_t index) {
        NS_LOG_FUNCTION_NOARGS(); 
        // std::cout<<"In End Send " << std::endl;
        Ptr<Packet> packetCopy = packet->Copy();
        LoraTag tag;
        packet->PeekPacketTag(tag);
        lora_app->packetsSent.at(tag.GetSpreadingFactor()-7)++;
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        fHdr.SetAsUplink(); 
        packetCopy->RemoveHeader (mHdr);
        packetCopy->RemoveHeader (fHdr);
        LoraDeviceAddress address = fHdr.GetAddress();
        // packet->PrintPacketTags();
        // uint8_t tmp = tag.GetReceivePower();
        // if (tmp != 0){
        //     tuple<int,int,double,double> tmp_tuple; 
        //     tmp_tuple = make_tuple(unsigned(tag.GetSpreadingFactor()),unsigned(tag.GetDataRate()),(tag.GetReceivePower()),tag.GetFrequency());
        //     SentByEDs.push_back(tmp_tuple);
        //     // std::cout<<"Sent with SF --->> " << unsigned(tag.GetSpreadingFactor()) << std::endl;
        //     // std::cout<<"Sent with DataRate --->> " << unsigned(tag.GetDataRate()) << std::endl;
        //     // std::cout<<"Sent TX Power --->> " << (tag.GetReceivePower()) << std::endl;
        //     // std::cout<<"Sent In frequency --->> " << tag.GetFrequency() << std::endl;
        //     lora_app->Correct_ED_UL_transmissions++;

        // }
        //std::cout<<"Sending Packet to GW at " << Simulator::Now().GetSeconds() << std::endl;
        //--- Update the state of the end Device ---//
        lora_app->vehicles_States[(address)] = "TX_F";
        // std::cout<<"Out End Send " << std::endl;
        //NS_LOG_FUNCTION_NOARGS();
    }

    //--- Callback to handle the reception of packet send from the GW/NS to the ED ---//
    void CorrectReceptionEDCallback(Ptr<Packet const> packet, uint32_t recepientId){
        NS_LOG_FUNCTION_NOARGS(); 
        Ptr<Packet> myPacket = packet->Copy();
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        LoraTag tag;
        myPacket->RemoveHeader (mHdr);
        myPacket->RemoveHeader (fHdr);
        myPacket->PeekPacketTag(tag);
        
        //Πάρε την θέση και τον κόμβο από τον αντίστοιχο container. 
        uint32_t tmp = recepientId-lora_app->stationary_nodes;
        Ptr<Node> cur_node = lora_app->m_vehicles.Get(tmp);
        LoraDeviceAddress addr = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>()->GetDeviceAddress(); 

        LoraDeviceAddress txAddress = fHdr.GetAddress();

        LoraTxParameters txParams;
        //txParams.sf = tag.GetSpreadingFactor();
        Time onair = cur_node->GetDevice(0)->GetObject<LoraNetDevice> () -> GetPhy() -> GetOnAirTime(myPacket,txParams); 
        OnAir.push_back(onair);
        

        lora_app->phy_end_reception_count.push(tmp); //Take every node that receives a message. 
        lora_app->ignore_broadcast_nodes.push(true);

        if (addr == lora_app->sensor_Address){
            if (addr == txAddress){std::cout<<"DAWDAWDAWDAWDAWDAWDAWDADAWDAWDAWDAWD ACK for Sensor"<<std::endl;}
            return; 
        }
        
        //Παίρνουμε τις συσκευές που ανήκουν στο δίκτυο. 
        std::map<LoraDeviceAddress, Ptr<EndDeviceStatus>> endDeviceStatuses = lora_app->m_ns->GetNetworkStatus()->m_endDeviceStatuses;
        
        //Ελέγχουμε αν ο κόμβος που δέχθηκε με αναμετάδοση το μήνυμα ανήκει στο δίκτυο ή είναι στο εύρος κάλυψης. 
        auto exl_node = std::find(lora_app->excluded_nodes.begin(), lora_app->excluded_nodes.end(),cur_node);
        if(exl_node!=lora_app->excluded_nodes.end()){
            return; 

        }else if(lora_app->remainingNodes.find(cur_node) != lora_app->remainingNodes.end() && txAddress.IsBroadcast()){
                        
            //Αν η συσκευή που πήρε το μήνυμα είναι στις συσκευές που δεν έχουν πάρει απάντηση από την πύλη 
            //και το μήνυμα που μόλις έλαβε είναι Broadcast τότε, έλεγξε σε ποιά κατηγορία ανήκει το σήμα και η συσκευή. 
            if(endDeviceStatuses.find(addr)!=endDeviceStatuses.end()){
                //Αν η συσκευή έχει στείλει μήνυμα  και το broadcast μήνυμα είναι έκτακτο τότε 
                if(lora_app->EmergencyQueue.find(addr)!=lora_app->EmergencyQueue.end() && lora_app->event_occurred){
                    //η συσκευή ανήκει στις συσκευές που κινδυνεύουν οπότε μην αγνοήσεις το broadcast μήνυμα. 
                    lora_app->ignore_broadcast_nodes.pop(); 
                    lora_app->ignore_broadcast_nodes.push(false); 
                }
            }else if(!lora_app->event_occurred){
                //Αν η συσκευή δεν έχει στείλει μήνυμα και το broadcast μήνυμα δεν είναι έκτακτο τότε  
                //Μην αγνοήσεις το broadcast γιατί η είναι το discovery που πρέπει να λάβει η συσκευή. 
                lora_app->vehicles_States[(addr)] = "RX_F"; //Άλλαξε την κατάσταση της συσκευής σε Rx_F αφού τελειώνει την λήψη
                lora_app->ignore_broadcast_nodes.pop(); 
                lora_app->ignore_broadcast_nodes.push(false);
            }else if(lora_app->event_occurred){
                //Αν η συσκευή δεν έχει στείλει μήνυμα και το broadcast μήνυμα είναι έκτακτο τότε 
                if(lora_app->EmergencyQueue.find(addr)!=lora_app->EmergencyQueue.end()){
                    //η συσκευή ανήκει στις συσκευές που κινδυνεύουν οπότε μην αγνοήσεις το broadcast μήνυμα.
                    lora_app->ignore_broadcast_nodes.pop(); 
                    lora_app->ignore_broadcast_nodes.push(false);
                }
            }

        }else if(endDeviceStatuses.find(addr)!=endDeviceStatuses.end() && txAddress.IsBroadcast()){

            //Η συσκευή ανήκει στο δίκτυο και ελέγχουμε αν είναι στις συσκευές που κινδυνεύονυ και αν το μήνυμα είναι έκτακτο
            if(lora_app->EmergencyQueue.find(addr)!=lora_app->EmergencyQueue.end() && lora_app->event_occurred){
                
                if (lora_app->prev_veh_States.find(addr)!=lora_app->prev_veh_States.end()){
                    std::string prev_state = lora_app->prev_veh_States[addr]; 
                    if(prev_state.compare("TX_P") == 0 || prev_state.compare("TX_F") == 0 ){
                        

                        lora_app->vehicles_States[(addr)] = "TX_P";
                    }else{
                       lora_app->vehicles_States[(addr)] = "RX_F";  
                    }
                    lora_app->ignore_broadcast_nodes.pop(); 
                    lora_app->ignore_broadcast_nodes.push(false);
                }else{
                    lora_app->vehicles_States[(addr)] = "RX_F"; 
                    lora_app->ignore_broadcast_nodes.pop(); 
                    lora_app->ignore_broadcast_nodes.push(false);
                }
            }else if(!lora_app->event_occurred && lora_app->ACK_Devices.find(addr)!=lora_app->ACK_Devices.end()){
                std::string txstate = lora_app->vehicles_States[(addr)]; 
                if(txstate.compare("TX_P") == 0 || txstate.compare("TX_F") == 0 ){
                    //std::cout << "TX_sTATE ==>>  " << txstate << std::endl;
                    lora_app->ignore_broadcast_nodes.pop(); 
                    lora_app->ignore_broadcast_nodes.push(true);
                }else{
                    lora_app->vehicles_States[(addr)] = "RX_F";
                    lora_app->ignore_broadcast_nodes.pop(); 
                    lora_app->ignore_broadcast_nodes.push(false);  
                }   
            }
        
        //Αν όχι τότε αγνόησε το discovery Signal. 
        }else if(txAddress==addr){ 
            //Αν το μήνυμα που έλαβε η συσκευή είναι το ACK Πακέτο για την ίδια την συσκευή. 
            auto ackqueue = lora_app->ACK_Queue.find(addr);
            if (ackqueue != lora_app->ACK_Queue.end()){ 
                lora_app->ACK_Queue.erase(ackqueue); 
            }else{
                return; 
            }
            //Αν η συσκευή έχει ξαναλάβει μήνυμα τότε αγνόησε την απάντηση.  
            NS_LOG_DEBUG("Received Frame Header: ADR -- " <<fHdr.GetAdr()
                 <<" AdrAckReq -- " << fHdr.GetAdrAckReq()
                 <<" ACK -- "<< fHdr.GetAck()
                 <<" FPending -- "<< fHdr.GetFPending()
                 <<" FOptsLen -- "<< fHdr.GetFOptsLen()
                 <<" FCnt -- "<< fHdr.GetFCnt());

            NS_LOG_DEBUG("Received Spreading Factor --->> " << unsigned(tag.GetSpreadingFactor())
                     <<"Received In frequency --->> " << tag.GetFrequency()
                     <<"Received Power --->> " << unsigned(tag.GetReceivePower()));

            lora_app->ACK_Devices.insert(addr);   
            lora_app->Correct_ED_DL_receptions++;
            lora_app->Correct_ED_ACK_receptions++; 
            std::string current_state = lora_app->vehicles_States[addr];

            //Διέγραψε τη συσκευή από τις συσκευές που περιμένουν να λάβουν επιβεβαίωση και δεν είναι 
            auto remain = lora_app->remainingNodes.find(cur_node);
            if (remain != lora_app->remainingNodes.end()){
                lora_app->remainingNodes.erase(remain); 
            }

            if(current_state.compare(("TX_F"))==0  || current_state.compare(("RX_P"))==0 ){
                Ptr<ClassCEndDeviceLorawanMac> classC = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<ClassCEndDeviceLorawanMac>(); 
                classC -> OpenSecondReceiveWindow(false);
                lora_app->vehicles_States[addr] = "RX_P";
                Ptr<PeriodicSender> persender = cur_node->GetApplication(0)->GetObject<PeriodicSender>(); 
                persender->StopApplication();
            }

            // Ptr<ClassCEndDeviceLorawanMac> cmac = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<ClassCEndDeviceLorawanMac>(); 
            // cmac -> SetMType(LorawanMacHeader::UNCONFIRMED_DATA_UP); 
            // lora_app->perShot.SetPeriod(Seconds(300)); 
            // lora_app->perShot.Install(cur_node);
                
        }else if (txAddress!=addr){
            lora_app->Incorrect_ACK++; 
        }
    }

    void CreateEventCallback(Ptr<Packet> packet){
        NS_LOG_FUNCTION_NOARGS();
        std::cout<<"An emergency event id was triggered, sensor reacted to an abnormal activity! The size of the Emergency Queue is => " << lora_app->EmergencyQueue.size()<<std::endl;
        //Have a new remote node send an emergency message to the NS and the nerwork server replying to all the devices inside the network at that point in time 
        lora_app->emergency_time = Simulator::Now().GetSeconds(); 
        lora_app->event_occurred = true; 

        if(lora_app-> emergency_event_id < 1){
            //Count_Endangered.push_back(0);
            lora_app->emergency_event_id++; 
        }else if (lora_app->emergency_event_id >=1 ){
            lora_app->emergency_event_id+= 2;
        }
        
        // Reset την ουρά με τις συσκευές όπου έχουν απορρίψει το broadcast γιατί μπορεί να μην το χρειάζονται πλέον. 
        if(!lora_app->ignore_broadcast_nodes.empty()){
            while(!lora_app->ignore_broadcast_nodes.empty()){
                lora_app->ignore_broadcast_nodes.pop();
            }
        }
        //Για τον ίδιο λόγο reset την ουρά που συνδέει Phy & Mac. 
        if(!lora_app->phy_end_reception_count.empty()){
            while(!lora_app->phy_end_reception_count.empty()){
                lora_app->phy_end_reception_count.pop();
            }
        }

        //Rest variables to get different metrics for different events. 
        lora_app->ED_in_danger_Emergency=0;
        lora_app->Num_EDs_received_EM = 0; 
        if(!lora_app->EmergencyQueue.empty()){lora_app->EmergencyQueue.clear();} 

        lora_app->m_flag = 1000; 
        Ptr<MobilityModel> sensorMob = lora_app->sensorNode.Get(0)->GetObject<MobilityModel>();
        Ptr<NetworkStatus> net_status = lora_app->m_ns->GetNetworkStatus(); //Get the netStatus object to access information 
        std::map<LoraDeviceAddress, Ptr<EndDeviceStatus>> endDeviceStatuses = net_status->m_endDeviceStatuses; //Get the end device statuses that are in the network 
        //std::string vehicle_state ("TX_P");
        
        for(auto endDevice : endDeviceStatuses){
            std::string edstate = lora_app->vehicles_States[endDevice.first]; 
            if (lora_app->prev_veh_States.find(endDevice.first) == lora_app->prev_veh_States.end()){
                lora_app->prev_veh_States.insert(pair<LoraDeviceAddress, string>(endDevice.first,lora_app->vehicles_States[endDevice.first]));
            }else{
                lora_app->prev_veh_States[endDevice.first] = lora_app->vehicles_States[endDevice.first];
            }
            if(endDevice.first == lora_app->sensor_Address ){continue;} // Do'not include the sensor in the emergency message. 
            Ptr<EndDeviceLorawanMac> edmac = endDevice.second ->GetMac()->GetObject<EndDeviceLorawanMac>(); 
            Ptr<NetDevice> net = edmac->GetDevice();
            Ptr<Node> node = net->GetNode();
            Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac() ->GetObject<ClassCEndDeviceLorawanMac>();
            double distance = sensorMob->GetDistanceFrom(node->GetObject<MobilityModel>()); // Check the distance between sensor and the vehicles.             
            if(distance<=2400.0){
                lora_app->EmergencyQueue.insert(endDevice.first);
                lora_app->ED_in_danger_Emergency++;
            }

            if ((edstate.compare("TX_P")==0 || edstate.compare("TX_F")==0) && (lora_app->ACK_Devices.find(endDevice.first) != lora_app->ACK_Devices.end())){
                Ptr<EndDeviceStatus> status = endDeviceStatuses[endDevice.first];
                Ptr<EndDeviceLorawanMac> edmac = status->GetMac()->GetObject<EndDeviceLorawanMac>();
                Ptr<LoraPhy> phy = edmac->GetPhy(); 
                Ptr<Packet> replypacket = status -> GetCompleteReplyPacket(); 
                Ptr<ClassCEndDeviceLorawanMac> classC = status -> GetMac() ->GetObject<ClassCEndDeviceLorawanMac>();
                if (phy -> GetObject<EndDeviceLoraPhy>() -> IsTransmitting()){
                    std::cout << " REPLRRR ARGGGGGG GRAAAAAA RARAGAGARARA"<< std::endl;

                    classC-> postponeTransmission(lora_app->emergency_delay,replypacket);
                    phy -> GetObject<EndDeviceLoraPhy>() -> SwitchToStandby(); 
                }
                std::cout<<"Emergency inside the loop for devices "<<std::endl;
                classC-> OpenSecondReceiveWindow(false);
                continue; 
            }

            edLorawanMac ->OpenSecondReceiveWindow(false);
            lora_app->vehicles_States[endDevice.first] = "RX_P";
            
            // if((vehicle_state.compare(lora_app->vehicles_States[endDevice.first]))!=0){
            //     edLorawanMac ->OpenSecondReceiveWindow(false);
            //     lora_app->vehicles_States[endDevice.first] = "RX_P";
            // }
            // edLorawanMac -> OpenSecondReceiveWindow(false);
            // lora_app->vehicles_States[endDevice.first] = "RX_P";
        }

        //Count_Endangered.at(nSims) =  Count_Endangered.at(nSims) + (lora_app->ED_in_danger_Emergency);
        lora_app->perShot.SetPeriod(Seconds(10)); //Periodically send a message into the gateway.  
        lora_app->perShot.SetPacketSize(10); 
        lora_app->perShot.Install(lora_app->sensorNode);
        Ptr<PeriodicSender> app = lora_app->sensorNode.Get(0)->GetApplication(0)->GetObject<PeriodicSender>(); 
        app->StartApplication(); 

        lora_app->EDs_in_NS_at_Emergency = lora_app->m_ns->GetNetworkStatus()->CountEndDevices();
        NS_LOG_FUNCTION_NOARGS(); 
    }

    //--- Callback Function to handle the GW broadcast towards the end devices ---//
    void BroadcastGenerationCallback(Ptr<Packet> data, NodeContainer gateway, bool autonomousGW){
        NS_LOG_FUNCTION_NOARGS(); 

        //Diference in these other than that in the first the GW alone decides to sent the echo packet ,is the waiting time. 
        //In the first method the waiting time approaches values greater than 10s. However on the Second it is 0s. This is not what really happens. 
        //Wait time is the same but the time which we call to get the wait time is different.  
        if(!lora_app->ignore_broadcast_nodes.empty()){
            while(!lora_app->ignore_broadcast_nodes.empty()){
                lora_app->ignore_broadcast_nodes.pop();
            }
        }

        if(!lora_app->phy_end_reception_count.empty()){
            while(!lora_app->phy_end_reception_count.empty()){
                lora_app->phy_end_reception_count.pop();
            }
        }

        if (autonomousGW){
            //Gateway by itself sends away the broadcast. 
            std::map<Address,Ptr<NetDevice>> gwAddresses = AvailableGWToSend(gateway, LoraDeviceAddress(0xFF, 0xFFFFFFFF),2,data); 

            Ptr<Packet> packet = CreateBroadcastPacket(data); 
            if(!gwAddresses.empty()){
                for (auto& address : gwAddresses){                
                    (address.second)->Send(packet,address.first,0x0800);
                    CreateSubsets(); 
                    Ptr<GatewayLorawanMac> gwMac = (address.second)->GetObject<LoraNetDevice>()->GetMac()->GetObject<GatewayLorawanMac>(); 
                    Time waitTime = gwMac -> GetWaitingTime(869.525);
                    lora_app->gw_next_broad.push(waitTime); 
                    gwMac -> TxFinished(packet);
                }
            } 
        }else{
            std::map<Address,Ptr<NetDevice>> gwAddresses = AvailableGWToSend(gateway, LoraDeviceAddress(0xFF, 0xFFFFFFFF),2,data); 
            if(!gwAddresses.empty()){
                for (auto& address : gwAddresses){
                    Ptr<GatewayLorawanMac> gwMac = (address.second)->GetObject<LoraNetDevice>()->GetMac()->GetObject<GatewayLorawanMac>(); 
                    Time waitTime = gwMac -> GetWaitingTime(869.525);
                    lora_app->gw_next_broad.push(waitTime);
                }
            }
            Simulator::ScheduleNow(&ns3::NSSendPacketCallback , lora_app->m_ns, Create<Packet>(10),LoraDeviceAddress(0xFF, 0xFFFFFFFF)); 
        }
        lora_app->broadcast_time = Simulator::Now();
        //NS_LOG_FUNCTION_NOARGS(); 
    }

    void OccupiedReceptionPathsCallback(int m_receptionPaths, int systemID){
        //NS_LOG_FUNCTION_NOARGS();
        if (systemID==0){
            Ptr<Node> gateway = lora_app->m_rspus.Get(0); 
            Ptr<GatewayLoraPhy> gwPhy = gateway->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<GatewayLoraPhy>(); 
            gwPhy->ResetReceptionPaths(); 
            int m_availablePaths = 0; 
            while(m_availablePaths<8){
                gwPhy->AddReceptionPath(); 
                m_availablePaths++; 
            }
        }
        //NS_LOG_FUNCTION_NOARGS();
    }

    void CorrectReceptionMACCallback(Ptr<Packet const> packet){
        
        NS_LOG_FUNCTION_NOARGS();
        Ptr<Packet> myPacket = packet->Copy();
        LorawanMacHeader mHdr;
        LoraFrameHeader fHdr;
        LoraTag tag; 
        fHdr.SetAsDownlink(); 
        myPacket->RemoveHeader (mHdr);
        myPacket->RemoveHeader (fHdr);
        myPacket->RemovePacketTag(tag); 
        LoraDeviceAddress txAddress = fHdr.GetAddress();
        
        uint32_t iter = lora_app->phy_end_reception_count.front(); //Iter =  recepientId - stationary nodes. 
        //The node that received the pkt

        Ptr<Node> cur_node = lora_app->m_vehicles.Get((iter));
        LoraDeviceAddress addr = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>()->GetDeviceAddress(); 
        lora_app->phy_end_reception_count.pop(); 
        bool answ = lora_app->ignore_broadcast_nodes.front();
        lora_app->ignore_broadcast_nodes.pop();
        if (addr == lora_app->sensor_Address){
            return; 
        }
        auto exl_node = std::find(lora_app->excluded_nodes.begin(), lora_app->excluded_nodes.end(),cur_node);
        if(exl_node!=lora_app->excluded_nodes.end()){
            //Device out of range. 
            return; 
        }
        if(!txAddress.IsBroadcast() && txAddress==addr){
            //An acknowledgement packet was received
            return; 
        }else if (!txAddress.IsBroadcast() && txAddress!=addr){
            return; 
        }

        if(txAddress.IsBroadcast()){
            //Find out what to do with the End device. 

            if(answ){return;}
            lora_app->Correct_ED_DL_receptions++;
            
            if(lora_app->event_occurred){
                if(!lora_app->EmergencyQueue.empty()){
                        lora_app->Num_EDs_received_EM++;
                        lora_app->EmergencyQueue.erase(lora_app->EmergencyQueue.begin());
                }else{
                    NS_LOG_DEBUG("Not an emergency device detected emergency broadcast"); 
                    return;
                }
                
            }else{
                Ptr<NetworkStatus> net_status = lora_app->m_ns->GetNetworkStatus(); //Get the netStatus object to access information 
                std::map<LoraDeviceAddress, Ptr<EndDeviceStatus>> endDeviceStatuses = net_status->m_endDeviceStatuses;
                if(endDeviceStatuses.find(addr)!=endDeviceStatuses.end()){
                    std::string vehicle_state ("RX_F"); 
                    if((vehicle_state.compare(lora_app->vehicles_States[addr]))==0){
                        if (lora_app->ACK_Devices.find(addr)!=lora_app->ACK_Devices.end()){
                            Ptr<EndDeviceLorawanMac> edMacWan = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>(); 
                            Ptr<EndDeviceStatus> edstatus = CreateObject<EndDeviceStatus>(addr,edMacWan);
                            Address gwAddress = (lora_app->m_gatewayStatuses.begin()->first);
                            
                            if(!lora_app->m_gatewayStatuses.empty()){lora_app->m_gatewayStatuses.erase(lora_app->m_gatewayStatuses.begin()->first);}
                            edstatus->InsertReceivedPacket(packet, gwAddress);
                            lora_app->vehicles_States[(addr)] = "TX_P";
                            Ptr<LorawanMac> m_mac = (cur_node)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac(); 
                            Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = m_mac->GetObject<ClassCEndDeviceLorawanMac>(); 
                            edLorawanMac -> SetMType(LorawanMacHeader::UNCONFIRMED_DATA_UP); 
                            //edLorawanMac -> SetMType(LorawanMacHeader::CONFIRMED_DATA_UP); //Σκάει και με τα δύο. 
                            // Ptr<PeriodicSender> app = cur_node->GetApplication(0)->GetObject<PeriodicSender>();
                            // app->StopApplication(); 
                            
                            std::cout<<"Number of Applications " << cur_node->GetNApplications() << std::endl;
                            if(cur_node->GetNApplications()== 2){
                                Ptr<PeriodicSender> app = cur_node->GetApplication(1)->GetObject<PeriodicSender>();
                                app->StartApplication();
                            }else{
                                lora_app->perShot.SetPeriod(Seconds(250)); 
                                lora_app->perShot.Install(cur_node);
                                Ptr<PeriodicSender> app = cur_node->GetApplication(1)->GetObject<PeriodicSender>();
                                app->StartApplication();
                            }
                            
                            // Simulator::Schedule(Seconds(5),[cur_node](){
                            //     Ptr<PeriodicSender> app = cur_node->GetApplication(0)->GetObject<PeriodicSender>();
                            //     app->StopApplication(); 
                            //     lora_app->perShot.SetPeriod(Seconds(250)); 
                            //     lora_app->perShot.Install(cur_node);
                            //     std::cout<<"Number of Applications " << cur_node->GetNApplications() << std::endl;

                            //     app = cur_node->GetApplication(1)->GetObject<PeriodicSender>();
                            //     app->StartApplication(); 
                            // }); 
                        }
                    }
                }else{
                    //No emergency has happened and broadcast received because device was in range. 
                    std::string vehicle_state ("RX_F"); 
                    if((vehicle_state.compare(lora_app->vehicles_States[addr]))==0){
                        Ptr<EndDeviceLorawanMac> edMacWan = cur_node->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>(); 
                        Ptr<EndDeviceStatus> edstatus = CreateObject<EndDeviceStatus>(addr,edMacWan);
                        lora_app->m_endDeviceStatuses.insert(pair<LoraDeviceAddress, Ptr<EndDeviceStatus>>(addr,edstatus)); 
                        Address gwAddress = (lora_app->m_gatewayStatuses.begin()->first);
                        if(!lora_app->m_gatewayStatuses.empty()){lora_app->m_gatewayStatuses.erase(lora_app->m_gatewayStatuses.begin()->first);}
                        edstatus->InsertReceivedPacket(packet, gwAddress);
                        lora_app->vehicles_States[(addr)] = "TX_P";
                    }    

                    if(lora_app->m_endDeviceStatuses.size() == lora_app->applicable_nodes){
                        //If all available devices have received the echo message then start sending. 
                        uint32_t subsetIdx = 0; 
                        Time delay_time = Seconds(0); 
                        while(subsetIdx != lora_app->Subsets.size()){
                            int numReplies = std::min(lora_app->maxReceptionPaths, static_cast<int>(lora_app->Subsets[subsetIdx].size()));
                            std::set<Ptr<Node>> temporary_set = lora_app->Subsets[subsetIdx];
                            for(auto edNode : temporary_set){
                                if(numReplies > 0 ){
                                    Ptr<LorawanMac> m_mac = (*edNode).GetDevice(0)->GetObject<LoraNetDevice>()->GetMac(); 
                                    Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = m_mac->GetObject<ClassCEndDeviceLorawanMac>(); 
                                    edLorawanMac -> SetMType(LorawanMacHeader::CONFIRMED_DATA_UP); 
                                    Simulator::Schedule((delay_time),[edNode](){
                                        lora_app->perShot.SetPeriod(Seconds(lora_app->SimulationTime/36)); 
                                        lora_app->perShot.Install(edNode);
                                        Ptr<PeriodicSender> app = edNode->GetApplication(0)->GetObject<PeriodicSender>();
                                        app->StartApplication(); 
                                    }); 

                                    delay_time +=  MilliSeconds(100); 
                                    --numReplies; 
                                    auto posi =  lora_app->Subsets[subsetIdx].find(edNode); 
                                    lora_app->Subsets[subsetIdx].erase(posi);
                                }
                            }

                            if (lora_app->Subsets[subsetIdx].empty()){
                                delay_time += Seconds(subsetIdx*5); 
                                subsetIdx++; 
                            }
                        }
                    }
                }       
            }
        }
        //NS_LOG_FUNCTION_NOARGS();
    }

    class DynamicTransmissions : public LoraApp{
      public:
        DynamicTransmissions(); 
        void RunSimulation() override;
        static void ConfigureSimulation(Ptr<DynamicTransmissions> classInstance); 
        void WriteCsvHeader();
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
        void SetConfigFromGlobals();
        void SetGlobalsFromConfig();
        void Run();
        
        std::string m_CsvFileName; 
        std::string m_performance; 
        std::string m_nodesfile;
        std::string m_gatewayfile;
        std::string m_sensorPerformance;
        std::string m_sensornodefile;
        std::string m_gatewaysensorfile;
        std::string m_traceFile; 
        std::string m_adrType; 
        std::string m_logFile;
        std::ofstream m_os;
        std::string m_loadConfigFileName; 
        std::string m_saveConfigFileName; 
        std::string fileNameWithNoExtension;
        std::string graphicsFileName; 
        std::string plotFileName; 
        std::string plotTitle; 
        std::string dataTitle; 

        uint8_t m_nwkId; //Network Id parameter to generate a unique identifier.
        uint32_t m_nwkAddr; //Network address parameter to generate a unique identifier.  
        uint32_t m_initial_nodes; //The initial count of nodes. 
        uint32_t m_nNodes; //Number of nodes to be created. 
        uint32_t m_nRspu; //Number of GW (for the time only one can be used)
        uint32_t m_packetSize; //The size of the packet for the echo broadcast. 
        double m_TotalSimTime; //The simulation time in seconds. 
        double m_radius; //the radius of the LoRa communications. 
        bool m_buildings; //If buildings will be created and taken into account. 
        bool m_realisticChannel; //If the channel will consider shadowing/fading. 
        bool m_log; //Enable logging into a file. 
        bool m_verbose; //Print the buildings if they are created. 
        bool m_adr; //Enable the Adr functionality. 
        bool m_additional_node_creation; //Determine if the additional node creation will be applied. 
        bool m_emergency; //Determine if emergency scenario is applied. 
        uint32_t m_emergency_time; //How many emergencies will occur. 

        Gnuplot2dDataset dataset; 
        Ptr<LoraChannel> m_channel; 
        Ptr<LoraDeviceAddressGenerator> m_addrGen; 
        NodeContainer m_networkServer;
        NetworkServerHelper m_networkServerHelper; 
        Ptr<FlowMonitor> m_monitor; 

    };


    // Φτιάξε ένα dedicated κανάλι επικοινωνίας μόνο για τα broadcasts 
    


    NS_OBJECT_ENSURE_REGISTERED(DynamicTransmissions);

    //--- DynamicTransmissions Class Functions ---//
    DynamicTransmissions::DynamicTransmissions()
        :m_CsvFileName("Lora-experiment.output.csv"),  
        m_performance("EDsPerformance.txt"),
        m_nodesfile("NodesFile.txt"),
        m_gatewayfile("GatewayFile.txt"),
        m_sensorPerformance("sensorPerformance.txt"), 
        m_sensornodefile("SensorNodeFile.txt"),
        m_gatewaysensorfile("GatewaysensorFile.txt"),
        m_traceFile(""),
        m_adrType("ns3::AdrComponent"),
        m_logFile(""), 
        m_os(),
        m_loadConfigFileName("config.txt"), 
        m_saveConfigFileName("config"),
        fileNameWithNoExtension("plot-2d"),
        graphicsFileName(""), 
        plotFileName("plotfile"),
        plotTitle("2-D Plot"),
        dataTitle("2-D Data"),
        m_nwkId(54), 
        m_nwkAddr(1864),
        m_initial_nodes(201),
        m_nNodes(1), 
        m_nRspu(1),
        m_packetSize(10),  
        m_TotalSimTime(3600.0), 
        m_radius(6400),
        m_buildings(false),
        m_realisticChannel(false),
        m_log(true), 
        m_verbose(false),
        m_adr(true),
        m_additional_node_creation(false),
        m_emergency(false), 
        m_emergency_time(1)
        
    {   
        m_channel = ConfigureChannel(); 
        graphicsFileName =  fileNameWithNoExtension + ".png";
        plotFileName = fileNameWithNoExtension + ".plt";
    }

    //--- Decleration of global variables ---//
    static ns3::GlobalValue g_nNodes("LEnNodes","Number of Vehicles from trace file",ns3::UintegerValue(203), ns3::MakeUintegerChecker<uint32_t>());
    static ns3::GlobalValue g_nRspu("LEnRspu","Number of RSPUs in the network",ns3::UintegerValue(5),ns3::MakeUintegerChecker<uint32_t>());
    static ns3::GlobalValue g_packetSize("LEpacketSize","Pcaket size in bytes",ns3::UintegerValue(50), ns3::MakeUintegerChecker<uint32_t>());
    static ns3::GlobalValue g_totalSimTime("LEtotalSimTime","Total Sim time (s)",ns3::DoubleValue(3600.0), ns3::MakeDoubleChecker<double>());
    static ns3::GlobalValue g_CsvFileName ("LECsvFileName","CSV filename (for time series data)",ns3::StringValue("Lora-experiment.output.csv"), ns3::MakeStringChecker());
    static ns3::GlobalValue g_traceFile("LEtraceFile","Mobility trace filename",ns3::StringValue("/home/jimborg/sumo/tools/2023-06-13-10-54-49/traffic_mob.tcl"), ns3::MakeStringChecker());
    static ns3::GlobalValue g_logFile("LElogFIle","Log filename",ns3::StringValue("log-lora-net-end-c.filt.7.adj.log"), ns3::MakeStringChecker());
    static ns3::GlobalValue g_realisticChannel("LErealisticChannel","Realistic channel propagation",ns3::BooleanValue(false), ns3::MakeBooleanChecker());
    static ns3::GlobalValue g_verbose("LEverbose","Enable verbose",ns3::BooleanValue(false), ns3::MakeBooleanChecker());
    static ns3::GlobalValue g_radius("LEradius","Available radius for communications",ns3::DoubleValue(6400.0), ns3::MakeDoubleChecker<double>());
    static ns3::GlobalValue g_buildings("LEbuildings","Enable creation of buildings for grid",ns3::BooleanValue(false), ns3::MakeBooleanChecker());
    static ns3::GlobalValue g_additional_node_creation("LEadditional_node_creation","If multiple nodes will be created during execution",ns3::BooleanValue(false), ns3::MakeBooleanChecker()); 
    static ns3::GlobalValue g_emergency_time("LEemergency_time", "The times which an emergency will occur in the system",ns3::UintegerValue(0), ns3::MakeUintegerChecker<uint32_t>()); 
    static ns3::GlobalValue g_emergency("LEemergency", "Create the conditions of an emergency to the system", ns3::BooleanValue(false), ns3::MakeBooleanChecker());

    //--- Enable Logging for the simulation. Use export NS_LOG=DynamicTransmissions ---//
    void DynamicTransmissions:: EnableLogging(){
        NS_LOG_FUNCTION("Enabling Logging for the simulation. Use export NS_LOG=DynamicTransmissions"); 
        LogComponentEnable("DynamicTransmissions", LOG_LEVEL_ALL);
        //LogComponentEnable("AdrComponent", LOG_LEVEL_ALL);
        //LogComponentEnable("NetworkServer", LOG_LEVEL_ALL);  
        //LogComponentEnable("NetworkScheduler", LOG_LEVEL_ALL);
        //LogComponentEnable("NetworkStatus", LOG_LEVEL_ALL); 
        //LogComponentEnable("LoraNetDevice", LOG_LEVEL_ALL);
        //LogComponentEnable("LogicalLoraChannelHelper", LOG_LEVEL_ALL);
        //LogComponentEnable("NetworkController", LOG_LEVEL_ALL);
        //LogComponentEnable("LoraChannel", LOG_LEVEL_INFO); //*******************
        //LogComponentEnable("LoraPhy", LOG_LEVEL_ALL);
        //LogComponentEnable("EndDeviceLoraPhy", LOG_LEVEL_ALL);
        // LogComponentEnable("GatewayLoraPhy", LOG_LEVEL_ALL);
        // LogComponentEnable("SimpleGatewayLoraPhy", LOG_LEVEL_ALL);
        // LogComponentEnable("GatewayLorawanMac", LOG_LEVEL_ALL);
        // LogComponentEnable("GatewayStatus",LOG_LEVEL_ALL);
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
        //LogComponentEnable ("AdrComponent", LOG_LEVEL_ALL);
        //LogComponentEnable("AnimationInterface", LOG_LEVEL_ALL); 

        m_os.open(m_logFile); 
        Packet::EnablePrinting(); 
        NS_LOG_FUNCTION_NOARGS();
    }

    //--- Gets the input of the cmd and initializes the values of the basic variables ---//
    void DynamicTransmissions::ParseCommandLineArguments(int argc, char** argv){
        NS_LOG_FUNCTION("Enabling commands to be parsed!"); 
        
        
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
        cmd.AddValue("additional_node_creation", "Add the creation of nodes during run", m_additional_node_creation); 
        cmd.AddValue("emergencyTimes", "The number of emergencies that will occur in the system (int)" , m_emergency_time); 
        cmd.AddValue("emergency", " Create the conditions for the emergency event(0=false, 1=true)", m_emergency); 
        
        cmd.Parse(argc,argv); 

        ConfigStoreHelper configStoreHelper; 
        configStoreHelper.LoadConfig(m_loadConfigFileName); 
        SetConfigFromGlobals(); 
        cmd.Parse(argc,argv); 

        ConfigureDefaults(); 
        SetGlobalsFromConfig();
        configStoreHelper.SaveConfig(m_saveConfigFileName);

        //m_traceFile = "/home/jimborg/sumo/tools/2023-06-13-10-54-49/traffic_mob.tcl";
        m_traceFile = "/home/jimborg/sumo/tools/2024-01-19-19-28-38/ns2LoraMobility.tcl";

        m_logFile = "log-lora-net-end-c.filt.7.adj.log" ;
        m_nNodes = 201; 
        m_nRspu = 1; 
        m_initial_nodes = 2164;
        lora_app->stationary_nodes += m_nRspu; 
        lora_app->SimulationTime =  m_TotalSimTime; 
        m_CsvFileName = "lora-experiment.csv";
        m_emergency_time = 4; 
        if (m_buildings){
            HandleBuildings();
        }
        if(m_verbose){
            EnableLogging();
        }
        ConfigureInitialNodes(); 
        NS_LOG_FUNCTION_NOARGS();
    }

    //--- Setting the configuration file from global class variables and initialize them from cmd  ---//
    void DynamicTransmissions::SetConfigFromGlobals(){
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
        GlobalValue::GetValueByName("LEadditional_node_creation",booleanValue); 
        m_additional_node_creation = booleanValue.Get();
        GlobalValue::GetValueByName("LEemergency_time", uintegerValue); 
        m_emergency_time = uintegerValue.Get(); 
        GlobalValue::GetValueByName("LEemergency",booleanValue); 
        m_emergency = booleanValue.Get();
        NS_LOG_FUNCTION_NOARGS();
    }

    //--- Set the global variables from the configuration file  ---//
    void DynamicTransmissions::SetGlobalsFromConfig(){
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
        g_additional_node_creation.SetValue(BooleanValue(m_additional_node_creation));
        g_emergency_time.SetValue(UintegerValue(m_emergency_time)); 
        g_emergency.SetValue(BooleanValue(m_emergency));
        NS_LOG_FUNCTION_NOARGS();
    }

    //--- Configure the channel to be used  ---//
    Ptr<LoraChannel> DynamicTransmissions::ConfigureChannel(){
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
            NS_LOG_FUNCTION_NOARGS();
    }

    //--- Configure the Helpers depending on the occassion ---//
    void DynamicTransmissions::ConfigureHelpers(uint32_t state,NodeContainer nodes){
        NS_LOG_FUNCTION(state); 
        switch (state){
            case 0: //Initialization 
                {
                    Ptr<LoraDeviceAddressGenerator> addgen = CreateObject<LoraDeviceAddressGenerator> (60,1900);
                    // Η σημαντική επικοινωνία γίνεται σε συγκεκριμένο κανάλι επικοινωνίας για να διασφαλιστεί η επιτυχία της επικοινωνίας
                    lora_app->m_critical_phyHelper = LoraPhyHelper();
                    lora_app->m_critical_phyHelper.SetChannel(lora_app->DedicatedChannel);
                    lora_app->m_critical_macHelper = LorawanMacHelper(); 
                    lora_app->m_critical_loraHelper = LoraHelper(); 
                    lora_app->m_critical_loraHelper.EnablePacketTracking();
                    lora_app->m_critical_phyHelper.SetDeviceType (LoraPhyHelper::ED);
                    lora_app->m_critical_macHelper.SetDeviceType (LorawanMacHelper::ED_A);
                    lora_app->m_critical_macHelper.SetAddressGenerator (addgen);
                    lora_app->m_critical_macHelper.SetRegion (LorawanMacHelper::EU);
                    lora_app->m_critical_loraHelper.EnableSimulationTimePrinting(Seconds(1000)); 
                    m_nwkId = m_nwkId;
                    m_nwkAddr = m_nwkAddr;  
                    m_addrGen = CreateObject<LoraDeviceAddressGenerator> (m_nwkId,m_nwkAddr);
                    lora_app->m_phyHelper = LoraPhyHelper();
                    lora_app->m_phyHelper.SetChannel(lora_app->m_loraChannel);
                    lora_app->m_macHelper = LorawanMacHelper();   
                    lora_app->m_loraHelper = LoraHelper(); 
                    lora_app->m_loraHelper.EnablePacketTracking(); 
                    lora_app->m_phyHelper.SetDeviceType (LoraPhyHelper::ED);
                    lora_app->m_macHelper.SetDeviceType (LorawanMacHelper::ED_C);
                    lora_app->m_macHelper.SetAddressGenerator (m_addrGen);
                    lora_app->m_macHelper.SetRegion (LorawanMacHelper::EU);
                    lora_app->m_loraHelper.EnableSimulationTimePrinting(Seconds(1000)); 

                    NS_LOG_DEBUG("Completed Initial creation of helpers for the two channels.");
                    break;
                }
            case 1: //Configure channels for Sensor. 
                {
                    lora_app->m_critical_loraHelper.Install(lora_app->m_critical_phyHelper,lora_app->m_critical_macHelper,nodes);
                    lora_app->sensor_Address = (nodes.Get(0)->GetDevice(0)->GetObject<LoraNetDevice>()->GetMac()->GetObject<EndDeviceLorawanMac>()->GetDeviceAddress());
                    NS_LOG_DEBUG("Installation of channel into the sensor complete.");
                    break; 
                }
            case 2: //Configure the vehicles 
                {
                    lora_app->m_loraHelper.Install(lora_app->m_phyHelper, lora_app->m_macHelper, nodes);
                    break; 
                }
            case 4: //Only install helper onto the new nodes.
                {
                    lora_app->m_critical_phyHelper.SetDeviceType (LoraPhyHelper::GW);
                    lora_app->m_phyHelper.SetDeviceType (LoraPhyHelper::GW);
                    lora_app->m_critical_macHelper.SetDeviceType (LorawanMacHelper::GW);
                    lora_app->m_macHelper.SetDeviceType (LorawanMacHelper::GW);
                    lora_app->m_critical_macHelper.SetRegion(LorawanMacHelper::EU); 
                    lora_app->m_macHelper.SetRegion(LorawanMacHelper::EU); 
                    lora_app->m_critical_loraHelper.Install (lora_app->m_critical_phyHelper, lora_app->m_critical_macHelper, nodes);
                    lora_app->m_loraHelper.Install (lora_app->m_phyHelper, lora_app->m_macHelper, nodes);
                    break;
                } 
            case 6: // Install the necessary helpers to the server node 
                {
                    lora_app->m_critical_macHelper.SetSpreadingFactorsUp (lora_app->sensorNode, nodes, lora_app->DedicatedChannel);
                    lora_app->m_macHelper.SetSpreadingFactorsUp (lora_app->m_vehicles, nodes, lora_app->m_loraChannel);
                    lora_app->m_critical_loraHelper.EnablePeriodicDeviceStatusPrinting(lora_app->sensorNode, nodes, m_sensornodefile, Seconds(1000),1); 
                    lora_app->m_loraHelper.EnablePeriodicDeviceStatusPrinting(lora_app->m_vehicles, nodes, m_nodesfile, Seconds(1000),2); 
                    lora_app->m_critical_loraHelper.EnablePeriodicGlobalPerformancePrinting(m_sensorPerformance, Seconds(1000)); 
                    lora_app->m_loraHelper.EnablePeriodicGlobalPerformancePrinting(m_performance, Seconds(1000)); 
                    lora_app->m_critical_loraHelper.EnablePeriodicPhyPerformancePrinting(nodes, m_gatewaysensorfile, Seconds(1000)); 
                    lora_app->m_loraHelper.EnablePeriodicPhyPerformancePrinting(nodes, m_gatewayfile, Seconds(1000)); 
                    break;
                }
            case 8: 
                {
                    m_nwkId = m_nwkId +  1;
                    m_nwkAddr = m_nwkAddr + 10;  
                    m_addrGen = CreateObject<LoraDeviceAddressGenerator> (m_nwkId,m_nwkAddr);
                    lora_app->m_phyHelper.SetDeviceType (LoraPhyHelper::ED);
                    lora_app->m_macHelper.SetDeviceType (LorawanMacHelper::ED_C);
                    lora_app->m_macHelper.SetAddressGenerator(m_addrGen);   
                    lora_app->m_macHelper.SetRegion(LorawanMacHelper::EU); 
                    lora_app->m_loraHelper.Install(lora_app->m_phyHelper, lora_app->m_macHelper, nodes);
                    break;
                }
            default:
                {
                    break;
                }
        }
        NS_LOG_FUNCTION_NOARGS();
    }

    //---  Creating the mobility of the new devices ---//
    NodeContainer DynamicTransmissions::CreateNodes(uint32_t state, bool traceMobility,int index_before,  uint32_t number_nodes, std::string tracefile,double radius){
       
        NS_LOG_FUNCTION(traceMobility << index_before << number_nodes << tracefile << radius);
        lora_app->addition_event_id++; 
        lora_app->creation_flag = true; 
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
                vel_mob -> SetVelocity(Vector(2.0 + vel,vel,0.0));
                new_vehicles.Get(ii)-> AggregateObject(vel_mob); 
            }
            mobilityVehicles.Install(new_vehicles); 
        }
    
        //Βάλε τους ύψος να μην είναι στο έδαφος.
        for(NodeContainer::Iterator jj = lora_app->m_vehicles.Begin(); jj!=lora_app->m_vehicles.End(); ++jj){
            //Get the vehicles off the ground into a height. 
            Ptr<MobilityModel> vel_mob = (*jj)->GetObject<MobilityModel>();
            Vector position = vel_mob->GetPosition();
            position.z = 1.2; 
            vel_mob->SetPosition(position); 
        }

        //Creation of clusters to store up to 8 devices to communicate with the gateway directly. 
        NS_LOG_DEBUG("Creation of Nodes was successfull with count :: " << lora_app->vehicles_States.size());
        NS_LOG_FUNCTION_NOARGS();
        return new_vehicles; 
    }

    //--- Configure the initial state of the simulation. Starting Nodes for GW, NS, EDs. ---//
    void DynamicTransmissions::ConfigureInitialNodes(){
        
        NS_LOG_FUNCTION_NOARGS(); 
        std::vector<double> frequencies; 
        frequencies.push_back(868.1);
        frequencies.push_back(868.3);
        frequencies.push_back(868.5);
        frequencies.push_back(868.8);
        frequencies.push_back(867.1);
        frequencies.push_back(867.3);
        frequencies.push_back(867.5);
        int reception_paths = 0; 

        //Create the channel for the experiment. 
        lora_app->m_loraChannel = ConfigureChannel();
        lora_app->DedicatedChannel = ConfigureChannel();
        //Set Up the initial Phy, Mac and Lora Helpers. 
        ConfigureHelpers(0, m_networkServer);
  
        /*******************Creating the Network Server node************************/
        MobilityHelper mobilityNs;
        Ptr<ListPositionAllocator> positionAllocNs = CreateObject<ListPositionAllocator> ();
        //positionAllocNs->Add (Vector (0.0,0.0,0.0));
        positionAllocNs->Add (Vector (1869.96,2542.11,10)); 
        mobilityNs.SetPositionAllocator (positionAllocNs);
        mobilityNs.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
        m_networkServer.Create(1); 
        mobilityNs.Install(m_networkServer);

        /*******************Creating the RSPU nodes ************************/
        MobilityHelper mobilityGw;
        Ptr<ListPositionAllocator> positionAllocGw = CreateObject<ListPositionAllocator> ();
        //positionAllocGw->Add (Vector (710.59,497.63,15.0));
        positionAllocGw->Add (Vector (2078.15,1532.55,15.0)); 
        mobilityGw.SetPositionAllocator (positionAllocGw);
        mobilityGw.SetMobilityModel ("ns3::ConstantPositionMobilityModel"); 
        lora_app->m_rspus.Create(m_nRspu);
        mobilityGw.Install(lora_app->m_rspus); 
        
        /******************* Creating the additional sensor node in the edge of the network *****************/        
        lora_app->sensorNode.Create(1);
        MobilityHelper sensor_mobility; 
        Ptr<ListPositionAllocator> positionSensor = CreateObject<ListPositionAllocator> ();
        //positionSensor->Add (Vector (5710.59,497.63,10.0));
        //positionSensor->Add (Vector (1132.64,157.29,15.0)); Position for first scenario
        positionSensor->Add (Vector (782.09, 513.90, 10.0)); 
        sensor_mobility.SetPositionAllocator (positionSensor);
        sensor_mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
        sensor_mobility.Install(lora_app->sensorNode); 
        ConfigureHelpers(1,lora_app->sensorNode); //Προσθέτουμε την εφαρμογή στον σένορα με το σημαντικό κανάλι. 
        Ptr<LorawanMac> edMacsensor = lora_app->sensorNode.Get(0)->GetDevice(0)->GetObject<LoraNetDevice> ()->GetMac ();
        LogicalLoraChannelHelper channelHelper = edMacsensor->GetLogicalLoraChannelHelper();
        Ptr<LogicalLoraChannel> criticalChannel = CreateObject<LogicalLoraChannel> (868.8, 0, 5);
        Ptr<LogicalLoraChannel> lc1 = CreateObject<LogicalLoraChannel> (868.1, 0, 5);
        Ptr<LogicalLoraChannel> lc2 = CreateObject<LogicalLoraChannel> (868.3, 0, 5);
        Ptr<LogicalLoraChannel> lc3 = CreateObject<LogicalLoraChannel> (868.5, 0, 5);
        channelHelper.RemoveChannel(lc1); 
        channelHelper.RemoveChannel(lc2);
        channelHelper.RemoveChannel(lc3);
        channelHelper.AddChannel(criticalChannel);    
        edMacsensor->SetLogicalLoraChannelHelper(channelHelper);   
        Ptr<EndDeviceLoraPhy> edPhysensor = lora_app->sensorNode.Get(0)->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<EndDeviceLoraPhy>();  
        edPhysensor -> TraceConnectWithoutContext("StartSending", MakeCallback(&ns3::EdDeviceSenttoPhyCallback));
        edMacsensor -> TraceConnectWithoutContext("CannotSendBecauseDutyCycle", MakeCallback(&ns3::EDSentRejectionCallback));
        edMacsensor -> TraceConnectWithoutContext("RequiredTransmissions", MakeCallback(&ns3::FinishedTransmissionCallback));
        edPhysensor -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(&ns3::CorrectReceptionEDCallback));

        edMacsensor -> GetObject<EndDeviceLorawanMac>()->SetMType(LorawanMacHeader::UNCONFIRMED_DATA_UP);
        edMacsensor -> GetObject<EndDeviceLorawanMac>()->SetDataRate(0); 
        
        NS_LOG_DEBUG("Created the sensor node with helpers.");
        /*******************Creating the vehicle nodes helpers and installing them ************************/
        m_addrGen = CreateObject<LoraDeviceAddressGenerator> (m_nwkId,m_nwkAddr);
        NodeContainer new_vehicles = CreateNodes(2,true, 0, m_initial_nodes,m_traceFile,m_radius); //Βάζουμε στις συσκευές την εφαρμογή για το κατάλληλο κανάλι. 
        lora_app->m_vehicles.Add(new_vehicles); 
        ConfigureHelpers(2,lora_app->m_vehicles); 
        
        //Connect the devices to the trace sources to get real time data and results based on callbacks. 
        for(NodeContainer::Iterator jj = lora_app->m_vehicles.Begin(); jj!=lora_app->m_vehicles.End(); ++jj){
        
            //Get the Mac and Phy Layer for the End device and connect them to their trace sources. 
            Ptr<LorawanMac> edMac = (*jj)->GetDevice (0)->GetObject<LoraNetDevice> ()->GetMac ();
            Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = edMac->GetObject<ClassCEndDeviceLorawanMac>();
            LoraDeviceAddress address = edLorawanMac->GetDeviceAddress();
            Ptr<EndDeviceLoraPhy> edPhy = (*jj)->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<EndDeviceLoraPhy>(); 
            LogicalLoraChannelHelper channelHelper = edMac->GetLogicalLoraChannelHelper();
            // Ptr<LogicalLoraChannel> lc1 = CreateObject<LogicalLoraChannel>(868.2,0,5);
            // Ptr<LogicalLoraChannel> lc2 = CreateObject<LogicalLoraChannel>(869.0,0,5);
            // Ptr<LogicalLoraChannel> lc3 = CreateObject<LogicalLoraChannel>(869.1,0,5);
            // channelHelper.AddChannel(lc1);
            // channelHelper.AddChannel(lc2);
            // channelHelper.AddChannel(lc3);
            
            //The End Device is in listening Mode. 
            lora_app->vehicles_States.insert(pair<LoraDeviceAddress, std::string>(address,"RX_P"));             
            
            //End device Opens second reception window due to frequency used by the Gateway to broadcast the message. 
            edLorawanMac->OpenSecondReceiveWindow(false); //Indefinetly open second receive window
            
            //Add the newly created Device into a Bimap to use later. 
            lora_app->m_createdNodes.insert({(*jj), address}); 

            //Trace Sources to be used by the End Device.  
            edPhy -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(&ns3::CorrectReceptionEDCallback));
            edMac -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(&ns3::CorrectReceptionMACCallback));
            edPhy -> TraceConnectWithoutContext("StartSending", MakeCallback(&ns3::EdDeviceSenttoPhyCallback));
            edMac -> TraceConnectWithoutContext("CannotSendBecauseDutyCycle", MakeCallback(&ns3::EDSentRejectionCallback));
            edMac -> TraceConnectWithoutContext("RequiredTransmissions", MakeCallback(&ns3::FinishedTransmissionCallback)); 
            edLorawanMac->SetMType (LorawanMacHeader::CONFIRMED_DATA_UP);

        }

        NS_LOG_DEBUG("Succesfully created the ed nodes + sensors.");
        ConfigureHelpers(4, lora_app->m_rspus); //Βάζουμε τις κατάλληλες εφαρμογές επικοινωνίας στις πύλες. 
        ConfigureHelpers(6, lora_app->m_rspus); 
        // Connect the gateways to the trace sources needed to keep track of communications. 
        for(NodeContainer::Iterator jj = lora_app->m_rspus.Begin(); jj != lora_app->m_rspus.End(); jj++){
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetMac() -> TraceConnectWithoutContext("SentNewPacket",MakeCallback(&ns3::GwSentPacketCallback)); 
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("PhyRxBegin",MakeCallback(&ns3::GWPhyRxBeginCallback));
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> GetObject<GatewayLoraPhy>() -> TraceConnectWithoutContext("OccupiedReceptionPaths",MakeCallback(&ns3::OccupiedReceptionPathsCallback)); 
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("LostPacketBecauseInterference",MakeCallback(&ns3::OnReceiveInterferenceGWCallback));
            (*jj) -> GetDevice(0) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(GWReceptioOfReplyPacket));
            (*jj) -> GetDevice(1) -> GetObject<LoraNetDevice> () -> GetMac() -> TraceConnectWithoutContext("SentNewPacket",MakeCallback(&ns3::GwSentPacketCallback)); 
            (*jj) -> GetDevice(1) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("PhyRxBegin",MakeCallback(&ns3::GWPhyRxBeginCallback));
            (*jj) -> GetDevice(1) -> GetObject<LoraNetDevice> () -> GetPhy() -> GetObject<GatewayLoraPhy>() -> TraceConnectWithoutContext("OccupiedReceptionPaths",MakeCallback(&ns3::OccupiedReceptionPathsCallback)); 
            (*jj) -> GetDevice(1) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("LostPacketBecauseInterference",MakeCallback(&ns3::OnReceiveInterferenceGWCallback));
            (*jj) -> GetDevice(1) -> GetObject<LoraNetDevice> () -> GetPhy() -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(GWReceptioOfReplyPacket));
            Ptr<GatewayLoraPhy> gwPhy = (*jj)->GetDevice(1)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<GatewayLoraPhy> ();
            gwPhy -> RemoveFrequencies();
            for(auto &f : frequencies){
                gwPhy->AddFrequency(f); 
            }
            gwPhy -> ResetReceptionPaths(); 
            while (reception_paths < lora_app->maxReceptionPaths){
                gwPhy->GetObject<GatewayLoraPhy>()->AddReceptionPath(); 
                reception_paths++; 
            }
        }
        
        /******************* Installing Devices to the network ************************/
        m_networkServerHelper.SetGateways(lora_app->m_rspus); 
        m_networkServerHelper.EnableAdr(m_adr);
        m_networkServerHelper.SetAdr(m_adrType);
        ApplicationContainer apps = m_networkServerHelper.Install(m_networkServer); 
        lora_app->m_ns = DynamicCast<NetworkServer>(apps.Get(0)); 
        lora_app->m_forwarderHelper.Install(lora_app->m_rspus); 
        
        //--- Initial Broadcast to get the initial nodes of the network ---//
        Simulator::Schedule(Seconds(3), &ns3::BroadcastGenerationCallback, Create<Packet>(m_packetSize),lora_app->m_rspus,true);
        Simulator::Schedule(Seconds(600), &ns3::BroadcastGenerationCallback, Create<Packet>(m_packetSize),lora_app->m_rspus,true);
        Simulator::Schedule(Seconds(1200), &ns3::BroadcastGenerationCallback, Create<Packet>(m_packetSize),lora_app->m_rspus,true);
        Simulator::Schedule(Seconds(1800), &ns3::BroadcastGenerationCallback, Create<Packet>(m_packetSize),lora_app->m_rspus,true);
        Simulator::Schedule(Seconds(2400), &ns3::BroadcastGenerationCallback, Create<Packet>(m_packetSize),lora_app->m_rspus,true);
        Simulator::Schedule(Seconds(3000), &ns3::BroadcastGenerationCallback, Create<Packet>(m_packetSize),lora_app->m_rspus,true);
        Simulator::Schedule(Seconds(3600), &ns3::BroadcastGenerationCallback, Create<Packet>(m_packetSize),lora_app->m_rspus,true);

        if(m_emergency){
            std::random_device rd; 
            std::mt19937 gen(rd()); //Marsenne Twister engine
            double correctness = 100.0; 
            std::uniform_real_distribution<double> distribution(correctness, 300.0);
            double diff = 600; 
            for(uint32_t emergency = 0 ; emergency < m_emergency_time; emergency++){
                    double multitude = distribution(gen); 
                    if(emergency>1){
                        std::uniform_real_distribution<double> newones(correctness, lora_app->SimulationTime);
                        multitude = newones(gen);  
                    }
                    if(abs(multitude-correctness) > diff){
                        correctness = multitude; 
                        Simulator::Schedule(Seconds(multitude),&ns3::CreateEventCallback,Create<Packet>(m_packetSize));
                    }correctness = multitude; 
            }   
        }
        NS_LOG_DEBUG("The Number of initial registered devices associated with the channel :: "<<lora_app->m_loraChannel->GetNDevices());
        NS_LOG_DEBUG("Finished Initializing the network, added vehicles and configured the channels.");
        lora_app->addition_event_id++;
        NS_LOG_FUNCTION_NOARGS(); 
    }

//--- Sets default attributes depending on the classes called by the program. ---//
    void DynamicTransmissions::ConfigureDefaults()
    {
        NS_LOG_FUNCTION_NOARGS(); 
    }

    //--- The Call to execute the simulation ---//
    void DynamicTransmissions::RunSimulation(){
        NS_LOG_FUNCTION_NOARGS(); 
        Run(); 
    }

    //--- The Execution of the simulation ---//
    void DynamicTransmissions::Run(){

        // FlowMonitorHelper flowhelper;
        // m_monitor = flowhelper.InstallAll(); 
        NS_LOG_FUNCTION_NOARGS(); 
        if(!m_additional_node_creation){
            AnimationInterface anim("LoRaApp.xml");
            NS_LOG_DEBUG("Animation Confirmed..."); 
        }

        Simulator::Stop(Seconds(m_TotalSimTime)); 
        Simulator::Run(); 
        // m_monitor->CheckForLostPackets(); 

        // std::map<FlowId, FlowMonitor::FlowStats> stats = m_monitor->GetFlowStats(); 
        ProcessOutputs(); 
        
        Simulator::Destroy(); 
        NS_LOG_FUNCTION_NOARGS();
    }
    
    //--- Handles the creation of buildings to alter the channel parameters. Cannot be showcased in NetAnim ---//
    void DynamicTransmissions::HandleBuildings (){
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
        
        BuildingsHelper::Install(lora_app->m_vehicles); 
        BuildingsHelper::Install(lora_app->m_rspus); 

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
        NS_LOG_FUNCTION_NOARGS();
    }
    
    void DynamicTransmissions::ConfigureSimulation(Ptr<DynamicTransmissions> classInstance){
        NS_LOG_FUNCTION_NOARGS();
        lora_app->addition_event_id++; 
        auto project_pointer = classInstance; 
        if(project_pointer->m_additional_node_creation){
            std::random_device rd; 
            std::mt19937 gen(rd());
            std::uniform_int_distribution<int> distribution(100,300);
            int multitude = distribution(gen);  

            /******************* Creating the new devices of the network ************************/
            NodeContainer new_vehicles = project_pointer->CreateNodes(4,false, project_pointer->m_initial_nodes,multitude,"",project_pointer->m_radius); 
            
            /******************* Installing the phy and mac layer helpers to the new devices ************************/
            lora_app->m_vehicles.Add(new_vehicles); 
            project_pointer->ConfigureHelpers(8, lora_app->m_vehicles);
            project_pointer->m_initial_nodes = lora_app->m_vehicles.GetN(); 

            //Connect the devices to the trace sources to get real time data and results based on callbacks. 
            for(NodeContainer::Iterator jj = lora_app->m_vehicles.Begin(); jj!=lora_app->m_vehicles.End(); ++jj){
            
                //Get the Mac and Phy Layer for the End device and connect them to their trace sources. 
                Ptr<LorawanMac> edMac = (*jj)->GetDevice (0)->GetObject<LoraNetDevice> ()->GetMac ();
                Ptr<ClassCEndDeviceLorawanMac> edLorawanMac = edMac->GetObject<ClassCEndDeviceLorawanMac>();
                LoraDeviceAddress address = edLorawanMac->GetDeviceAddress();
                Ptr<EndDeviceLoraPhy> edPhy = (*jj)->GetDevice(0)->GetObject<LoraNetDevice>()->GetPhy()->GetObject<EndDeviceLoraPhy>(); 

                //The End Device is in listening Mode. 
                lora_app->vehicles_States.insert(pair<LoraDeviceAddress, std::string>(address,"RX_P"));             

                //End device Opens second reception window due to frequency used by the Gateway to broadcast the message. 
                edLorawanMac->OpenSecondReceiveWindow(false); //Indefinetly open second receive window
                
                //Add the newly created Device into a Bimap to use later. 
                lora_app->m_createdNodes.insert({(*jj), address}); 

                //Trace Sources to be used by the End Device.  
                edPhy -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(&ns3::CorrectReceptionEDCallback));
                edMac -> TraceConnectWithoutContext("ReceivedPacket",MakeCallback(&ns3::CorrectReceptionMACCallback));
                edPhy -> TraceConnectWithoutContext("StartSending", MakeCallback(&ns3::EdDeviceSenttoPhyCallback));
                edMac -> TraceConnectWithoutContext("CannotSendBecauseDutyCycle", MakeCallback(&ns3::EDSentRejectionCallback));
                edMac -> TraceConnectWithoutContext("RequiredTransmissions", MakeCallback(&ns3::FinishedTransmissionCallback)); 
                edLorawanMac->SetMType (LorawanMacHeader::CONFIRMED_DATA_UP);
            }

            lora_app->m_macHelper.SetSpreadingFactorsUp(lora_app->m_vehicles,lora_app->m_rspus,lora_app->m_loraChannel);

            Time diff = Simulator::Now() - lora_app->broadcast_time;
            Time next_broad = lora_app->gw_next_broad.front();
            lora_app->gw_next_broad.pop();  

            /******************* Set next echo broadcast based on duty cycle and emergency occurrance.  ************************/
            if(diff > next_broad  && !lora_app->event_occurred){
                Simulator::Schedule(Seconds(3), &ns3::BroadcastGenerationCallback, Create<Packet>(project_pointer->m_packetSize),lora_app->m_rspus,true);
            }else if(diff < next_broad && !lora_app->event_occurred){
                Simulator::Schedule(next_broad - diff, &ns3::BroadcastGenerationCallback, Create<Packet>(project_pointer->m_packetSize),lora_app->m_rspus,true);
            }else if(lora_app->event_occurred){
                Simulator::Schedule(Seconds(100), &ns3::BroadcastGenerationCallback, Create<Packet>(project_pointer->m_packetSize),lora_app->m_rspus,true);
            }else{
                Simulator::Schedule(Seconds(50),&ns3::BroadcastGenerationCallback, Create<Packet>(project_pointer->m_packetSize),lora_app->m_rspus,true);
            }
        }
        NS_LOG_FUNCTION_NOARGS();
    }

    //--- Process Outputs to Validate the Simulation ---//
    void DynamicTransmissions::ProcessOutputs(){
        NS_LOG_FUNCTION_NOARGS(); 
        LoraPacketTracker &tracker = lora_app->m_loraHelper.GetPacketTracker ();
        LoraPacketTracker &tracker1 = lora_app->m_critical_loraHelper.GetPacketTracker ();

        std::cout<<"************************************************************************************************************************************************************"<<std::endl;

        if(m_verbose){
            std::cout<<"//<< Presenting the Results >>//";
            std::cout<<"The Events for node creation: "<<std::endl;
            if(m_additional_node_creation){
                std::cout<<"Creation of extra nodes during execution produced these results: "<<std::endl;
                for(auto tupl : lora_app->NodeCreationVector){
                std::cout<<"Event ID => " << get<0>(tupl)
                    << " | Number of Nodes Created => " << get<1>(tupl)
                    << " | Final number of Applicable nodes => " << get<2>(tupl) << std::endl;
                }
            }   
        }
        
        uint32_t informed_eds = 0; 
        if(m_emergency){
            for(auto tupl : lora_app->EmergencyVector){
                 if(m_verbose){
                    std::cout<<"Event ID => " << get<0>(tupl)
                    << " | Number of Nodes In Danger => " << get<1>(tupl)
                    << " | Final number of Nodes Informed => " << get<2>(tupl) << std::endl;
                 }
                 informed_eds+=get<2>(tupl); 
            }
        }

        tuple<uint32_t,uint32_t> tmp; 
        tmp = make_tuple(lora_app->ED_in_danger_Emergency,informed_eds);
        Regard_EM.push_back(tmp);

        std::string pdr_string = tracker.CountMacPacketsGlobally(Seconds(0),Seconds(3600)); 
        std::string word; 
        std::stringstream ss(pdr_string); 
        std::vector<double> pdrs_val; 
        while(getline(ss,word, ' ')){
            pdrs_val.push_back(stod(word)); 
        }

        PDR_GW.push_back(pdrs_val[1]/pdrs_val[0]);
        PLR_GW.push_back(1-(pdrs_val[1]/pdrs_val[0]));
        PER_GW.push_back(tracker.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[2]/pdrs_val[0]);
        Count_Acc_EDs.push_back(lora_app->accepted_devices);

        std::cout<<"Total number of sent UL packets from EDs :: " << lora_app->Correct_ED_UL_transmissions<< std::endl;


        //Η συνάρτηση CountMacPacketGlobally returns number of sent packets and number of packets received by at least one gateway. 
        std::cout<<"Total number of send and received packets (device 1) :: " << tracker.CountMacPacketsGlobally(Seconds(0),Seconds(3600)) << std::endl;
        std::cout<<"Total number of send and received packets (device 2) :: " << tracker1.CountMacPacketsGlobally(Seconds(0),Seconds(3600)) << std::endl;

        //Η συνάρτηση CountMacPacketsGloballyCpsr returns the number of sent packets and the number of packets that generated a successful acknowledgment.
        std::cout<<"Mac packets with ACK reached (End Devices) => " << tracker.CountMacPacketsGloballyCpsr(Seconds(0),Seconds(m_TotalSimTime)) << std::endl;
        std::cout<<"Mac packets with ACK reached (Sensor) => " << tracker1.CountMacPacketsGloballyCpsr(Seconds(0),Seconds(m_TotalSimTime)) << std::endl;
        std::cout<<"ACK packets sent by the GW => " << lora_app->Correct_ACK_GW_transmissions << std::endl; 
       
        //Η συνάρτηση PrintPhyPacketsPerGW returns the number of Received Interferred NomoreReceivers UnderSensitivity LostBecauseNoTX Unset
        std::cout<<"Performance at PHY level of a specific gateway (Device[1]): " << tracker.PrintPhyPacketsPerGw(Seconds(0),Seconds(3600),1) << std::endl;
        std::cout<<"Performance at PHY level of a specific gateway (Device[2]): " << tracker1.PrintPhyPacketsPerGw(Seconds(0),Seconds(3600),1) << std::endl;
       
        //Η συνάρτηση CountPhyPacketsPerGw evaluate the performance at PHY level of a specific gateway.
        std::cout<<"Total Packets Sent (Device[1]): " << tracker.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[0] << std::endl;
        std::cout<<"Total Packets Sent (Device[2]): " << tracker1.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[0] << std::endl;
        std::cout<<"Total Packets Received (Device[1]): " << tracker.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[1] << std::endl;
        std::cout<<"Total Packets Received (Device[2]): " << tracker1.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[1] << std::endl;
        std::cout<<"Interfered Packets (Device[1]): " << tracker.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[2] << std::endl;
        std::cout<<"Interfered Packets (Device[2]): " << tracker1.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[2] << std::endl;
        std::cout<<"No More Gw Packets (Device[1]): " << tracker.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[3] << std::endl;
        std::cout<<"No More Gw Packets (Device[2]): " << tracker1.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[3] << std::endl;
        std::cout<<"Under Sensitivity Packets (Device[1]): " << tracker.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[4] << std::endl;
        std::cout<<"Under Sensitivity Packets (Device[2]): " << tracker1.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[4] << std::endl;
        std::cout<<"Lost Because No TX (Device[1]): " << tracker.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[5] << std::endl;
        std::cout<<"Lost Because No TX (Device[2]): " << tracker1.CountPhyPacketsPerGw(Seconds(0),Seconds(3600),1)[5] << std::endl;

        //Πόσες συσκευές έγιναν αποδεκτές στο δίκτυο 2 μετρικές 
        std::cout<<"Total Number of Accepted Nodes based on Network Status => "<<lora_app->m_ns->GetNetworkStatus()->CountEndDevices()<<std::endl; 
        std::cout<<"Total Number of Accepted Nodes based on program counter => "<<lora_app->accepted_devices + 1 << std::endl; 

        //Πόσα πακέτα broadcasts μεταδόθηκαν
        std::cout<<"Gateway System Id [ " << lora_app->gw_systemId<< " ]. "<<std::endl;
        std::cout<<"Total Number of Broadcasts (including discovery + EB) => " << lora_app->NS_success_broadcasts<<std::endl;
        std::cout<<"Gateway [ " << lora_app->gw_systemId <<" ] Finished Reception of all available ED replies at time => "<< lora_app->finished_gw_reception<<std::endl;

        //Πόσα πακέτα ACK έφτασαν στις συσκευές που έπρεπε
        std::cout<<"Total Number of ACK packets (based on program counter (ACK_Devices)) => " << lora_app->ACK_Devices.size() << std::endl;
        std::cout<<"Total Number of ACK packets (based on program counter (Correct_ED_ACK_receptions)) => " << lora_app->Correct_ED_ACK_receptions <<std::endl; 

        //Άλλες μετρικές όπως παρεμβολές και ακυρωμένες
        std::cout<<"Total Number of Interference Packets => " << lora_app->lost_packets_due_Intf <<std::endl;
        std::cout<<"Total Number of Cancelled Transmission from EDs => " << lora_app->failed_transmissions_EDs<<std::endl;        
        std::cout<<"Total Number of Cancelled Transmission from EDs due to DutyCycle => " << lora_app->lost_packets_due_DutyC <<std::endl;

        //Μετρικές σχετικά με τα έκτακτα πακέτα 
        std::cout<<"Total Number of Devices accepted in the Network at emergency => " << lora_app->EDs_in_NS_at_Emergency<<std::endl;
        std::cout<<"Total Number of Devices in danger at emergency => " << lora_app->ED_in_danger_Emergency<<std::endl;
        std::cout<<"************************************************************************************************************************************************************"<<std::endl;

        std::string pdr_string2 = tracker.CountMacPacketsGloballyCpsr(Seconds(0),Seconds(m_TotalSimTime)); 
        std::stringstream ss2(pdr_string2);
        std::vector<double> pdr_eds; 
            while(getline(ss2,word,' ')){
                pdr_eds.push_back(stod(word)); 
                //std::cout<<""<<word<<std::endl;
            }
        PDR_ED.push_back(pdr_eds[1]/pdr_eds[0]);
        PLR_ED.push_back(1-(pdr_eds[1]/pdr_eds[0]));
        PER_ED.push_back(lora_app->Incorrect_ACK/pdrs_val[0]); 
        
        Gnuplot plot(graphicsFileName);
        // Instantiate the plot and set its title.
        plot.SetTitle(plotTitle); 

        // Make the graphics file, which the plot file will create when it
        // is used with Gnuplot, be a PNG file.
        plot.SetTerminal("png"); 

        // Set the labels for each axis.
        plot.SetLegend("X Values" , "Y Values"); 
    
        // Set the range for the x axis.
        plot.AppendExtra("set xrange [0:+3600]"); 
        
        // Instantiate the dataset, set its title, and make the points be
        // plotted along with connecting lines.
        dataset.SetTitle(dataTitle); 
        dataset.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
        dataset.SetErrorBars(Gnuplot2dDataset::XY);

        double x; 
        double xErrorDelta; 
        double y; 
        double yErrorDelta; 

        for(auto tupl : lora_app->PDR){
            y = get<1>(tupl);
            x = get<2>(tupl).GetSeconds();
            xErrorDelta = 0.25;
            yErrorDelta = 0.1 * y;
            dataset.Add(x,y,xErrorDelta,yErrorDelta); 

        }
        plot.AddDataset(dataset); 
        std::ofstream plotFile(plotFileName); 

        plot.GenerateOutput(plotFile); 
        plotFile.close(); 
        m_os.close(); //Close log file
        NS_LOG_FUNCTION_NOARGS();
    }

    //--- Write results into a CSV file for further analysis ---//
    void DynamicTransmissions::WriteCsvHeader(){
        NS_LOG_FUNCTION_NOARGS(); 

        std::ofstream outputFile("LoRaApp_outputs.csv");
        if (outputFile.is_open()){
            outputFile << "PDR-GW,PLR-GW,PER_GW,PDR-ED,PLR-ED,PER_ED,AC,IDEDs,InformedEDS\n";
            
            for(uint32_t szi=0; szi < PDR_GW.size(); szi++){
                outputFile << PDR_GW.at(szi) <<","
                           << PLR_GW.at(szi) <<","
                           << PER_GW.at(szi) <<","
                           << PDR_ED.at(szi) <<","
                           << PLR_ED.at(szi) <<","
                           << PER_ED.at(szi) <<","
                           << Count_Acc_EDs.at(szi) <<","
                           << get<0>(Regard_EM.at(szi)) <<","
                           << get<1>(Regard_EM.at(szi)) <<'\n';
                std::cout<<"| PDR for The GW => " << PDR_GW.at(szi) <<" |"<<std::endl;
                std::cout<<"| PLR for the GW => " << PLR_GW.at(szi) <<" |"<<std::endl;
                std::cout<<"| PER for the GW => " << PER_GW.at(szi) <<" |"<<std::endl;
                std::cout<<"| PDR for The EDs => " << PDR_ED.at(szi) <<" |"<<std::endl;
                std::cout<<"| PLR for the EDs => " << PLR_ED.at(szi) <<" |"<<std::endl;
                std::cout<<"| PER for the ED => " << PER_ED.at(szi) << " |"<<std::endl;
                std::cout<<"Accepted Devices in Network =>" << Count_Acc_EDs.at(szi)<<std::endl; 
                std::cout<<"Indangered Devices In Event => "<<get<0>(Regard_EM.at(szi))<<std::endl; 
                std::cout<<"Informed Endangered Devices => " << get<1>(Regard_EM.at(szi))<<std::endl;
                //std::cout<<"Total Number of Endangered Devices => " << Count_Endangered.at(szi)<<std::endl;   
            }
            outputFile.close();
        }else{
            std::cerr<<"Error opening the file.\n";
        }
        //lora_app->m_loraHelper.PrintEndDevices(lora_app->m_vehicles,lora_app->m_rspus, "PrintEndDevices.dat"); 
    }
    
}

int main (int argc, char* argv[]){

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <argument1> <argument2> ..." << std::endl;
        return 1; // Exit with an error code
    }
    
    std::istringstream ss(argv[1]); 
    if (!(ss >> nSims)) {
        std::cerr << "Invalid number: " << argv[1] << '\n';
    } else if (!ss.eof()) {
        std::cerr << "Trailing characters after number: " << argv[1] << '\n';
    }

    if(nSims > 50){
        std::cerr <<"Wrong Number of simulations entered above maximum limit." << std::endl;
        return 1; 
    }else if (nSims < 0){
        std::cerr <<"Negative number not negotiable." << std::endl;
        return 1; 
    }

    int repetitions = nSims; 
    std::string originalString = "set xrange [0:50]";
    std::string counterString = std::to_string(nSims);
    size_t position = originalString.find(":50"); 
    if(position == std::string::npos){
        std::cerr <<"Something went Wrong." << std::endl;
        return 1; 
    }


    std::string filteredString = originalString.substr(0,position) + ":" + counterString +"]"; 

    std::string fileNameWithNoExtension("PDR-plot2d");
    std::string graphicsFileName;
    std::string plotFileName;
    std::string plotTitle("PDR-Packet Delivery Ratio");
    graphicsFileName =  fileNameWithNoExtension + ".png";
    plotFileName = fileNameWithNoExtension + ".plt";

    Gnuplot PDR_PER_PLR_EDs("PDR_PER_PLR_EDs.png");
    PDR_PER_PLR_EDs.SetTitle("PDR - PER - PLR (EDs)"); 
    PDR_PER_PLR_EDs.SetTerminal("png"); 
    PDR_PER_PLR_EDs.SetLegend("Simulation ID" , "PDR/PER/PLR"); 
    PDR_PER_PLR_EDs.AppendExtra(filteredString); 
    PDR_PER_PLR_EDs.AppendExtra("set yrange [0:1]"); 

    Gnuplot gnuplot = Gnuplot("channels.eps");
    gnuplot.SetTitle("EPS channels"); 
    gnuplot.SetTerminal("png"); 
    gnuplot.SetLegend("Simulation ID" , "PDR/PER/PLR"); 
    gnuplot.AppendExtra(filteredString); 
    gnuplot.AppendExtra("set yrange [0:1]"); 
 
    Gnuplot plot("PDR_PER_PLR_GW.png");
    plot.SetTitle("PDR - PER - PLR (GW)"); 
    plot.SetTerminal("png"); 
    plot.SetLegend("Simulation ID" , "PDR/PER/PLR"); 
    plot.AppendExtra(filteredString); 
    plot.AppendExtra("set yrange [0:1]"); 

    Gnuplot2dDataset dataset;   
    dataset.SetTitle("PDR-GW"); 
    dataset.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    dataset.SetErrorBars(Gnuplot2dDataset::XY);
    
    Gnuplot2dDataset dataset2; 
    dataset2.SetTitle("PDR-EDs"); 
    dataset2.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    dataset2.SetErrorBars(Gnuplot2dDataset::XY);

    Gnuplot2dDataset dataset3; 
    dataset3.SetTitle("PER-GW"); 
    dataset3.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    dataset3.SetErrorBars(Gnuplot2dDataset::XY); 

    Gnuplot2dDataset dataset4; 
    dataset4.SetTitle("PER-EDs"); 
    dataset4.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    dataset4.SetErrorBars(Gnuplot2dDataset::XY); 
    
    Gnuplot2dDataset dataset5; 
    dataset5.SetTitle("PLR-GW"); 
    dataset5.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    dataset5.SetErrorBars(Gnuplot2dDataset::XY); 

    Gnuplot2dDataset dataset6; 
    dataset6.SetTitle("PLR-EDs"); 
    dataset6.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    dataset6.SetErrorBars(Gnuplot2dDataset::XY); 
    
    Ptr<DynamicTransmissions> experiment; 

    auto start = std::chrono::high_resolution_clock::now(); 
    std::cout<<"Simulation Progress:\n"; 
    for(nSims = 0 ; nSims < repetitions ; ++nSims){  
        experiment = CreateObject<DynamicTransmissions>();  
        
        double progress = static_cast<double>(nSims)/repetitions; 
        uint32_t barwidth = 50;
        std::cout<< "["; 
        uint32_t pos = barwidth * progress;

        for(uint32_t j = 0; j < barwidth; ++j){ 
            if(j < pos) std::cout<<"="; 
            else if(j == pos) std::cout << ">"; 
            else std::cout << " " ; 
        }

        std::cout<< "] " << int(progress * 100.0) << "%\r";
        std::cout.flush();
        
        experiment -> Simulate(argc, argv);
        uint32_t additional_nodes_creation = 2; 
        std::random_device rd; 
        std::mt19937 gen(rd()); //Marsenne Twister engine
        double correctness = 600.0; 
        std::uniform_real_distribution<double> distribution(correctness, 3600.0);
        double diff = 200; 

        for(uint32_t emergency = 0 ; emergency < additional_nodes_creation; emergency++){
            double multitude = distribution(gen); 
            if(emergency>1){
                std::uniform_real_distribution<double> newones(correctness, 3600.0);
                multitude = newones(gen);  
            }
            if(abs(multitude-correctness) > diff){
                
                //Simulator::Schedule(Seconds(multitude),&DynamicTransmissions::ConfigureSimulation, &experiment);
                Simulator::Schedule(Seconds(multitude),&DynamicTransmissions::ConfigureSimulation, experiment);
            }
            correctness = multitude; 
        }

        experiment->RunSimulation(); 
        if((nSims+1) == repetitions){
            experiment->WriteCsvHeader();
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now(); 
    std::chrono::duration<double> duration = end - start ; 
    std::cout << "Simulation took " << duration.count() << " seconds." << std::endl;
    
    double x; 
    double xErrorDelta1,xErrorDelta2,xErrorDelta3,xErrorDelta4,xErrorDelta5,xErrorDelta6; 
    double y_pdr_gw,y_pdr_ed,y_per_gw,y_per_ed,y_plr_gw,y_plr_ed;
    double yErrorDelta1, yErrorDelta2, yErrorDelta3,yErrorDelta4, yErrorDelta5, yErrorDelta6; 

    for(uint32_t szi=0; szi < PDR_GW.size(); szi++){
        x = szi;
        y_pdr_gw = PDR_GW.at(x);
        y_pdr_ed = PDR_ED.at(x);
        y_per_gw = PER_GW.at(x);
        y_per_ed = PER_ED.at(x);
        y_plr_gw = PLR_GW.at(x); 
        y_plr_ed = PLR_ED.at(x);

        xErrorDelta1 = 0.05;
        xErrorDelta2 = 0.05;
        xErrorDelta3 = 0.05;
        xErrorDelta4 = 0.05;
        xErrorDelta5 = 0.05;
        xErrorDelta6 = 0.05;

        yErrorDelta1 = 0.05 * y_pdr_gw;
        yErrorDelta2 = 0.05 * y_pdr_ed;
        yErrorDelta3 = 0.05 * y_per_gw;
        yErrorDelta4 = 0.05 * y_per_ed;
        yErrorDelta5 = 0.05 * y_plr_gw;
        yErrorDelta6 = 0.05 * y_plr_ed;

        dataset.Add (x,y_pdr_gw,xErrorDelta1,yErrorDelta1); 
        dataset2.Add(x,y_pdr_ed,xErrorDelta2,yErrorDelta2); 
        dataset3.Add(x,y_per_gw,xErrorDelta3,yErrorDelta3);
        dataset4.Add(x,y_per_ed,xErrorDelta4,yErrorDelta4);
        dataset5.Add(x,y_plr_gw,xErrorDelta5,yErrorDelta5);
        dataset6.Add(x,y_plr_ed,xErrorDelta6,yErrorDelta6);
    }

    std::cout << " setting Plot" << std::endl;

    plot.AddDataset(dataset);
    PDR_PER_PLR_EDs.AddDataset(dataset2); 
    plot.AddDataset(dataset3);
    PDR_PER_PLR_EDs.AddDataset(dataset4);
    plot.AddDataset(dataset5); 
    PDR_PER_PLR_EDs.AddDataset(dataset6);

    std::ofstream plotFile(plotFileName); 
    plot.GenerateOutput(plotFile); 
    plotFile.close(); 
    
    std::ofstream pdr_per_eds("PDR_PER_PLR_EDs.plt"); 
    PDR_PER_PLR_EDs.GenerateOutput(pdr_per_eds); 
    pdr_per_eds.close(); 
    

    gnuplot.AddDataset(dataset);
    gnuplot.AddDataset(dataset2);
    gnuplot.AddDataset(dataset3);
    gnuplot.AddDataset(dataset4);
    gnuplot.AddDataset(dataset5);
    gnuplot.AddDataset(dataset6);
    
    std::ofstream plotFile2("gnuplotEx.plt"); 
    gnuplot.GenerateOutput(plotFile2); 
    plotFile2.close(); 

    PDR_GW.clear(); 
    PDR_ED.clear(); 
    PER_ED.clear(); 
    PER_GW.clear(); 
    PLR_GW.clear(); 
    PLR_ED.clear(); 
 

    struct packetInfo{
        double timeOnAir; 
        int sf; 
    };

    Gnuplot2dDataset onair_df;
    onair_df.SetTitle("OnAir Time(s)"); 
    onair_df.SetStyle(Gnuplot2dDataset::LINES_POINTS);
    Gnuplot2dDataset hist_df7;
    hist_df7.SetTitle("ToA(s) SF=7"); 
    hist_df7.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    Gnuplot2dDataset hist_df8;
    hist_df8.SetTitle("ToA(s) SF=8"); 
    hist_df8.SetStyle(Gnuplot2dDataset::LINES_POINTS); 
    Gnuplot2dDataset hist_df12;
    hist_df12.SetTitle("ToA(s) SF=12"); 
    hist_df12.SetStyle(Gnuplot2dDataset::LINES_POINTS);  
    double counter=0.0; 
    
    std::vector<packetInfo> packetData;
    std::map<double, int> airTimeCounts;

    for(auto t : OnAir){
        airTimeCounts[t.GetSeconds()]++; 
    }

    for(auto item : airTimeCounts){
        if(item.first==0.025856){
            hist_df7.Add(item.first,0);
            hist_df7.Add(item.first,item.second);
        }else if(item.first == 0.041216){
            hist_df8.Add(item.first,0);
            hist_df8.Add(item.first,item.second);
        }else if(item.first == 0.097536){
            hist_df12.Add(item.first,0);
            hist_df12.Add(item.first,item.second);
        }
    }

    // int total_packets = 0; 
    // double total_time = 0; 

    // for(auto const& [time, count] : airTimeCounts){
    //     int sf = -1;
    //     if(time ==0.097536) sf = 12; 
    //     else if(time == 0.041216) sf = 8; 
    //     else if(time == 0.025856) sf = 7; 

    // }

    Histogram hgm = Histogram(); 
    for(auto airtime: OnAir){
        if (airtime.GetSeconds()==0.097536){
            packetData.push_back({airtime.GetSeconds(), 12}); 
        }else if(airtime.GetSeconds()==0.041216){
            packetData.push_back({airtime.GetSeconds(), 8}); 
        }else if(airtime.GetSeconds()==0.025856){
            packetData.push_back({airtime.GetSeconds(), 7}); 
        }
        hgm.AddValue(airtime.GetSeconds()); 
        onair_df.Add(counter, airtime.GetSeconds()); 
        counter++; 
    }

    std::string pk_counter = std::to_string(counter); 
    std::string ft_string = originalString.substr(0,position) + ":" + pk_counter+"]"; 
    std::cout<<"Counter reached " << counter << std::endl;

    std::filebuf fb; 
    fb.open("histogram.xml", std::ios::out );
    std::ostream os(&fb);     
    int indent = 3; 
    std::string elementName = "SF_ToA Histogram"; 
    hgm.SerializeToXmlStream(os, indent, elementName); 
    fb.close();

    Gnuplot hgm_plot("Histogram.png");
    hgm_plot.SetTitle("Time of Air / Number of Packets");
    hgm_plot.SetTerminal("png"); 
    hgm_plot.SetLegend("Time of Air (s)", "Packets Count");
    hgm_plot.AppendExtra("set xrange [0:0.25]");
    hgm_plot.AddDataset(hist_df7);
    hgm_plot.AddDataset(hist_df8);
    hgm_plot.AddDataset(hist_df12);    
    std::ofstream histfile("Hist.plt"); 
    hgm_plot.GenerateOutput(histfile); 
    histfile.close(); 
    
    Gnuplot onair_plot("OnAirTime.png");
    onair_plot.SetTitle("OnAir Time of Response"); 
    onair_plot.SetTerminal("png"); 
    onair_plot.SetLegend("Packet ID" , "Time(s)"); 
    onair_plot.AppendExtra(ft_string);
    onair_plot.AddDataset(onair_df); 
    
    std::ofstream onairfile("OnAirTime.plt"); 
    onair_plot.GenerateOutput(onairfile); 
    onairfile.close(); 

    // Gnuplot gp; 
    // gp << "set boxwidth 0.002\n"; 
    // gp << "set style fill solid\n";  
    // gp << "set xlabel 'Time of air (sec)'\n";
    // gp << "set ylabel 'Number of Packets'\n";
    // gp << "set title 'Packet Transmission Time of Air Distribution'\n";
    // gp << "plot '-' using 1:2 with boxes notitle\n";
    // gp.send1d(packetData);  
    
    // std::cout<<"Accepted Devices in Network =>" << Count_Acc_EDs.at(szi)<<std::endl; 
    // std::cout<<"Indangered Devices In Event => "<<get<0>(Regard_EM.at(szi))<<std::endl; 
    // std::cout<<"Informed Endangered Devices => " << get<1>(Regard_EM.at(szi))<<std::endl;

    Gnuplot regard_plot("Informed Devices.png");
    regard_plot.SetTitle("Emergency Disaster"); 
    regard_plot.SetTerminal("png"); 
    regard_plot.SetLegend("In Danger","Informed"); 
    regard_plot.AppendExtra(filteredString); 
    regard_plot.AppendExtra("set yrange [0:2500]"); 

    Gnuplot2dDataset accepted;
    accepted.SetTitle("Accepted"); 
    accepted.SetStyle(Gnuplot2dDataset::LINES_POINTS);
    Gnuplot2dDataset indanger;
    indanger.SetTitle("In Danger"); 
    indanger.SetStyle(Gnuplot2dDataset::LINES_POINTS);
    Gnuplot2dDataset informed;
    informed.SetTitle("Informed"); 
    informed.SetStyle(Gnuplot2dDataset::LINES_POINTS);
    
    double y_accepted,y_in_danger,y_informed; 

    for(uint32_t szi=0; szi < Count_Acc_EDs.size(); szi++){
        x = szi;
        y_accepted = Count_Acc_EDs.at(x); 
        y_in_danger = get<0>(Regard_EM.at(x));
        y_informed = get<1>(Regard_EM.at(x));
        accepted.Add(x,0); 
        accepted.Add(x,y_accepted); 
        indanger.Add(x,0);
        indanger.Add(x,y_in_danger); 
        informed.Add(x,0);
        informed.Add(x,y_informed); 
    }

    regard_plot.AddDataset(accepted);
    regard_plot.AddDataset(indanger); 
    regard_plot.AddDataset(informed);
    std::ofstream regard_file("Regard_file.plt"); 
    regard_plot.GenerateOutput(regard_file); 
    regard_file.close(); 
    
    Count_Acc_EDs.clear(); 
    //Count_Endangered.clear(); 
    Regard_EM.clear();

    std::ofstream outputFile("sent_data_statistics.csv"); 
    if(outputFile.is_open()){
        for(uint32_t szi=0; szi<SentByEDs.size(); szi++){
            x = szi; //Packet
            outputFile << x << "," 
                       << get<0>(SentByEDs.at(szi)) << ","
                       << get<1>(SentByEDs.at(szi)) << ","
                       << get<2>(SentByEDs.at(szi)) << ","
                       << get<3>(SentByEDs.at(szi)) << endl;            
            // std::cout<<"Number of position " << SentByEDs.at(szi) << std::endl;
            // std::cout<<"Number of size " << SentByEDs.size() << " " << szi<< std::endl;
            
        }
        outputFile.close(); 
    }else {
        cerr<<"Error opening the file."<<endl; 
        return 1; 
    }
    
    std::cout<<"Execution of program finished with a total of " << repetitions << " repetitions. \nClosing simulation..." <<std::endl;
    return 0;

}