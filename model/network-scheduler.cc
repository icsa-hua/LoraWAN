/*
 * Copyright (c) 2017 University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Davide Magrin <magrinda@dei.unipd.it>
 * 
 * Modified by: QiuYukang <b612n@qq.com>
 * Modified by: icsa-hua <info_icsa@hua.gr>
 */

#include "network-scheduler.h"

namespace ns3 {
namespace lorawan {

NS_LOG_COMPONENT_DEFINE ("NetworkScheduler");

NS_OBJECT_ENSURE_REGISTERED (NetworkScheduler);

TypeId
NetworkScheduler::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::NetworkScheduler")
    .SetParent<Object> ()
    .AddConstructor<NetworkScheduler> ()
    .AddTraceSource ("ReceiveWindowOpened",
                     "Trace source that is fired when a receive window opportunity happens.",
                     MakeTraceSourceAccessor (&NetworkScheduler::m_receiveWindowOpened),
                     "ns3::Packet::TracedCallback")
    .SetGroupName ("lorawan");
  return tid;
}

NetworkScheduler::NetworkScheduler ()
{
}

NetworkScheduler::NetworkScheduler (Ptr<NetworkStatus> status,
                                    Ptr<NetworkController> controller) :
  m_status (status),
  m_controller (controller)
{
}

NetworkScheduler::~NetworkScheduler ()
{
}

void
NetworkScheduler::OnReceivedPacket (Ptr<const Packet> packet)
{
  NS_LOG_FUNCTION (packet);

  // Get the current packet's frame counter
  Ptr<Packet> packetCopy = packet->Copy ();
  LorawanMacHeader receivedMacHdr;
  packetCopy->RemoveHeader (receivedMacHdr);
  LoraFrameHeader receivedFrameHdr;
  receivedFrameHdr.SetAsUplink ();
  packetCopy->RemoveHeader (receivedFrameHdr);

  // Need to decide whether to schedule a receive window
  if (!m_status->GetEndDeviceStatus (packet)->HasReceiveWindowOpportunityScheduled ())
  {
    // Extract the address
    LoraDeviceAddress deviceAddress = receivedFrameHdr.GetAddress ();

    // Schedule OnReceiveWindowOpportunity event
    m_status->GetEndDeviceStatus (packet)->SetReceiveWindowOpportunity (
    Simulator::Schedule (Seconds (1),
                           &NetworkScheduler::OnReceiveWindowOpportunity,
                           this,
                           deviceAddress,
                           1)); // This will be the first receive window
  }
}

void
NetworkScheduler::OnReceiveWindowOpportunity (LoraDeviceAddress deviceAddress, int window)
{
  NS_LOG_FUNCTION (deviceAddress);

  NS_LOG_DEBUG ("Opening receive window number " << window << " for device "
                                                 << deviceAddress);

  // Check whether we can send a reply to the device, again by using
  // NetworkStatus
  Address gwAddress = m_status->GetBestGatewayForDevice (deviceAddress, window);

  if (gwAddress == Address () && window == 1)
    {
      NS_LOG_DEBUG ("No suitable gateway found for first window.");

      // No suitable GW was found, but there's still hope to find one for the
      // second window.
      // Schedule another OnReceiveWindowOpportunity event
      m_status->GetEndDeviceStatus (deviceAddress)->SetReceiveWindowOpportunity (
        Simulator::Schedule (Seconds (1),
                             &NetworkScheduler::OnReceiveWindowOpportunity,
                             this,
                             deviceAddress,
                             2));     // This will be the second receive window
    }
  else if (gwAddress == Address () && window == 2)
    {
      // No suitable GW was found and this was our last opportunity
      // Simply give up.
      NS_LOG_DEBUG ("Giving up on reply: no suitable gateway was found " <<
                   "on the second receive window");

      // Reset the reply
      // XXX Should we reset it here or keep it for the next opportunity?
      m_status->GetEndDeviceStatus (deviceAddress)->RemoveReceiveWindowOpportunity();
      m_status->GetEndDeviceStatus (deviceAddress)->InitializeReply ();
    }
  else
    {
      // A gateway was found

      NS_LOG_DEBUG ("Found available gateway with address: " << gwAddress);

      m_controller->BeforeSendingReply (m_status->GetEndDeviceStatus
                                          (deviceAddress));

      // Check whether this device needs a response by querying m_status
      bool needsReply = m_status->NeedsReply (deviceAddress);

      if (needsReply)
        {
          NS_LOG_INFO ("A reply is needed");

          // Send the reply through that gateway
          m_status->SendThroughGateway (m_status->GetReplyForDevice
                                          (deviceAddress, window),
                                        gwAddress);

          // Reset the reply
          m_status->GetEndDeviceStatus (deviceAddress)->RemoveReceiveWindowOpportunity();
          m_status->GetEndDeviceStatus (deviceAddress)->InitializeReply ();
        }
    }
}

void 
NetworkScheduler::DoSend(Ptr<Packet> data, LoraDeviceAddress deviceAddress, int window)
{
  NS_LOG_FUNCTION(this << data << deviceAddress << window);
  
  // Broadcast frame
  if (deviceAddress.IsBroadcast ()) 
    {
      // Find all avalible gateways to send broadcast
      std::list<Address> gwAddresses = m_status->GetAvalibleGatewaysForBroadcast ();
      if (gwAddresses.size() == 0) 
      {
        NS_LOG_INFO ("No avalible gateways for broadcast!");
        return;
      }

      // Create a broadcast frame
      Ptr<Packet> packet = m_status->CreateBroadcastPacket (data);

      // Send the packet through all avalible gateways
      for (auto it = gwAddresses.begin(); it != gwAddresses.end(); it++)
        {
          NS_LOG_DEBUG ("Send a broadcast frame through gateway " << *it << " .");
          m_status->SendThroughGateway (packet, *it);
        }
    } 
  // Unicast frame
  else 
    {
      // Check whether we can send a reply to the device, again by using
      // NetworkStatus
      Address gwAddress = m_status->GetBestGatewayForDevice (deviceAddress, window);

      NS_LOG_DEBUG ("Found available gateway with address: " << gwAddress);
      if(gwAddress == Address())
      {
        NS_LOG_DEBUG ("No suitable gateway found.");
        return;
      }

      // Create a frame
      Ptr<Packet> packet = m_status->GetDataPacketForDevice(data, deviceAddress, window);
      
      // Send the reply through that gateway
      m_status->SendThroughGateway (packet, gwAddress);
    }
}
}
}
