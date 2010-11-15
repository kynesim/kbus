/*
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the KBUS Lightweight Linux-kernel mediated
 * message system
 *
 * The Initial Developer of the Original Code is Kynesim, Cambridge UK.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Kynesim, Cambridge UK
 *   Richard Watts <rrw@kynesim.co.uk>
 *   Tony Ibbs <tibs@tonyibbs.co.uk>
 *
 * ***** END LICENSE BLOCK *****
 */

#include <iostream>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "cppkbus.h"

using namespace cppkbus;

int testMessageIds()
{
    MessageId  m1;
    assert(m1.ToString() == "[0,0]");

    MessageId  m2 = MessageId(1,2);
    assert(m2.ToString() == "[1,2]");

    assert(m2 > m1);

    OrigFrom  o1;
    assert(o1.ToString() == "[0,0]");

    OrigFrom  o2 = OrigFrom(1,2);
    assert(o2.ToString() == "[1,2]");

    assert(o2 > o1);

    return 0;
}

int testMessage()
{
    bool        boolValue;
    uint32_t    uint32Value;
    std::string strValue;
    MessageId   msgId;
    OrigFrom    origFrom;
    uint8_t     dataFred[4] = {0x66, 0x72, 0x65, 0x64};

    // Some simple tests of message creation
    Message msg1;
    assert(msg1.ToString() == "<EmptyMessage>");
    assert(msg1.IsEmpty());
    assert(msg1.GetName() == "");
    assert(msg1.GetData() == NULL);
    assert(msg1.GetDataLength() == 0);
    assert(msg1.GetFlags() == 0);
    assert(!msg1.IsRequest());
    assert(!msg1.IsStatefulRequest());
    assert(!msg1.WantsUsToReply());
    assert(!msg1.IsReply());
    assert(!msg1.IsReplierBindEvent());
    assert(msg1.GetReplierBindEventData(boolValue, uint32Value, strValue) == -1);
    msgId = MessageId(1,1);
    assert(msg1.GetMessageId(msgId) == -1);
    assert(msgId == MessageId(1,1));            // unchanged by the call
    assert(msg1.GetInReplyTo(msgId) == -1);
    assert(msgId == MessageId(1,1));            // ditto
    uint32Value = 1;
    assert(msg1.GetTo(uint32Value) == -1);
    assert(uint32Value == 1);                   // ditto
    assert(msg1.GetFrom(uint32Value) == -1);
    assert(uint32Value == 1);
    origFrom = OrigFrom(1,1);
    assert(msg1.GetOrigFrom(origFrom) == -1);   // ditto
    assert(origFrom == OrigFrom(1,1));
    assert(msg1.GetFinalTo(origFrom) == -1);
    assert(origFrom == OrigFrom(1,1));          // ditto

    Message msg2("$.Fred", dataFred, 4);
    assert(msg2.ToString() == "<Message \"$.Fred\" data=\"fred\">");
    assert(!msg2.IsEmpty());
    assert(msg2.GetName() == "$.Fred");
    // By default, we've taken a copy of the data
    assert(msg2.IsEntire());
    assert(msg2.GetData() != dataFred);
    assert(msg2.GetDataLength() == 4);

    Message msg3;
    msg3 = msg2;
    // The assignment operator works...
    assert(msg3.ToString() == "<Message \"$.Fred\" data=\"fred\">");

    // Assignment to the same object works
    msg3 = msg3;
    assert(msg3.ToString() == "<Message \"$.Fred\" data=\"fred\">");

    // The copy constructor works
    msg3 = Message(msg3);
    assert(msg3.ToString() == "<Message \"$.Fred\" data=\"fred\">");

    // If we choose, we can use our data directly
    // (and we can also set flags...)
    Message msg4("$.Fred", dataFred, 4, 0x1234, false);
    assert(msg4.GetData() == dataFred);
    assert(msg4.GetDataLength() == 4);
    assert(!msg4.IsEntire());
    assert (msg4.ToString() == "<Message \"$.Fred\" flags=1234 SYN|aFL data=\"fred\">");

    // If we copy (by operator=) the message, it is the default elementwise
    // copy of the contents
    Message msg5 = msg4;
    assert(msg5.GetData() == dataFred);
    assert(msg5.GetDataLength() == 4);
    assert(!msg5.IsEntire());
    assert(msg5.ToString() == "<Message \"$.Fred\" flags=1234 SYN|aFL data=\"fred\">");

    // We're going to need some messages that look as if they've been received...
    MessageId req1Id(0,12);
    MessageId req2Id(0,13);
    OrigFrom  ofrom1(1,12);
    OrigFrom  ofrom2(1,13);
    Message req1("$.Request", MessageFlags::WantReply, &req1Id, NULL, 0, 28);
    Message reqToUs("$.Request", MessageFlags::WantReply|MessageFlags::WantYouToReply,
                    &req2Id, NULL, 0, 28);
    Message stateReqToUs("$.Stateful.Request",
                         MessageFlags::WantReply|MessageFlags::WantYouToReply,
                         &req2Id, NULL, 5, 28);
    Message replyFrom("$.Reply", 0, &req1Id, &req2Id, 7, 9, &ofrom1);
    assert(req1.ToString() == "<Request \"$.Request\" id=[0,12] from=28 flags=1 REQ>");
    assert(reqToUs.ToString() == "<Request \"$.Request\" id=[0,13] from=28 flags=3 REQ|YOU>");
    assert(stateReqToUs.ToString() == "<Request \"$.Stateful.Request\" id=[0,13] to=5 from=28 flags=3 REQ|YOU>");
    assert(replyFrom.ToString() == "<Reply \"$.Reply\" id=[0,12] to=7 from=9 orig_from=[1,12] in_reply_to=[0,13]>");

    assert(req1.IsRequest() && !req1.IsStatefulRequest());
    assert(reqToUs.IsRequest() && !reqToUs.IsStatefulRequest());
    assert(stateReqToUs.IsRequest() && stateReqToUs.IsStatefulRequest());
    assert(replyFrom.IsReply());

    assert(replyFrom.GetFrom(uint32Value) == 0);
    assert(uint32Value == 9);
    assert(replyFrom.GetOrigFrom(origFrom) == 0);
    assert(origFrom == ofrom1);

    // We have more conventional ways to set flags
    assert(msg3.BecomesReplyTo(req1) == -EBADMSG);
    assert(msg3.BecomesReplyTo(reqToUs) == 0);
    assert(msg3.ToString() == "<Reply \"$.Fred\" to=28 in_reply_to=[0,13] data=\"fred\">");
    assert(msg3.IsReply());
    assert(msg3.GetInReplyTo(msgId) == 0);
    assert(msgId == req2Id);
    assert(msg3.GetTo(uint32Value) == 0);
    assert(uint32Value == 28);

    assert(msg4.BecomesStatefulRequest(replyFrom) == 0);
    // Still with the weird flags...
    assert(msg4.ToString() == "<Request \"$.Fred\" to=9 final_to=[1,12] flags=1235 REQ|SYN|aFL data=\"fred\">");
    assert(msg4.IsStatefulRequest());
    assert(msg4.GetFinalTo(origFrom) == 0);
    assert(origFrom == ofrom1);

    assert(msg4.BecomesStatefulRequest(msg2) == -EBADMSG);  // Wrong sort of message
    assert(msg4.BecomesStatefulRequest(stateReqToUs) == 0);
    assert(msg4.ToString() == "<Request \"$.Fred\" to=5 flags=1235 REQ|SYN|aFL data=\"fred\">");

    static Constants c = Constants::Get();

    // OK, this next bit is terribly architecture dependent...
    uint32_t repBindEventData1[] = {1, 23, 4, 0x66726564};
    Message repBindEvent1(c.kMessageNameReplierBindEvent,
                          (uint8_t *)repBindEventData1, 16);
    assert(repBindEvent1.ToString() == "<ReplierBindEvent [Bind \"derf\" for 23]>");

    // As is this...
    uint32_t repBindEventData2[] = {0, 24, 6, 0x64657266, 0x00006464};
    Message repBindEvent2(c.kMessageNameReplierBindEvent,
                          (uint8_t *)repBindEventData2, 18);
    assert(repBindEvent2.ToString() == "<ReplierBindEvent [Unbind \"freddd\" for 24]>");

    assert(repBindEvent2.GetReplierBindEventData(boolValue, uint32Value, strValue) == 0);
    assert(!boolValue);
    assert(uint32Value=24);
    assert(strValue == "freddd");

    Message msgSimple("$.James");
    assert(msgSimple.ToString() == "<Message \"$.James\">");

    // I suppose we should test this...
    Message *msg = new Message("$.SidJames");
    assert(msg->ToString() == "<Message \"$.SidJames\">");
    delete msg;

    return 0;
}

int testDevice()
{
    bool isSet;

    std::cout << "Testing Device code" << std::endl;

    Device dev0(0);

    std::cout << "Created device " << dev0 << std::endl;

    assert(dev0.ToString() == "<Device 0, \"/dev/kbus0\">");

    std::cout << "Making kernel more verbose: returns " <<
        dev0.MakeKernelVerbose(true) << std::endl;
    std::cout << "Checking if kernel is more verbose: returns " <<
        dev0.QueryKernelVerbose(isSet) << std::endl;
    if (isSet)
        std::cout << "   Apparently it is, good" << std::endl;
    else
    {
        std::cout << "   Oh dear, it is not" << std::endl;
        return 1;
    }

    std::cout << "Making kernel less verbose again: returns " <<
        dev0.MakeKernelVerbose(false) << std::endl;
    std::cout << "Checking if kernel is less verbose: returns " <<
        dev0.QueryKernelVerbose(isSet) << std::endl;
    if (!isSet)
        std::cout << "   Apparently it is, good" << std::endl;
    else
    {
        std::cout << "   Oh dear, it is not" << std::endl;
        return 1;
    }

    std::cout << "Asking for Replier Bind Events: returns " <<
        dev0.ReportReplierBinds(true) << std::endl;
    std::cout << "Checking if they will be reported: returns " <<
        dev0.ReportingReplierBinds(isSet) << std::endl;
    if (isSet)
        std::cout << "   Apparently they will, good" << std::endl;
    else
    {
        std::cout << "   Oh dear, they will not" << std::endl;
        return 1;
    }

    std::cout << "Asking to stop Replier Bind Events: returns " <<
        dev0.ReportReplierBinds(false) << std::endl;
    std::cout << "Checking if they will be reported: returns " <<
        dev0.ReportingReplierBinds(isSet) << std::endl;
    if (!isSet)
        std::cout << "   Apparently they will not, good" << std::endl;
    else
    {
        std::cout << "   Oh dear, they will" << std::endl;
        return 1;
    }

    unsigned newDeviceNumber;
    std::cout << "Asking for another device: returns " <<
        dev0.AddNewDevice(newDeviceNumber) << std::endl;
    std::cout << "New device number is " << newDeviceNumber << std::endl;

    std::cout << "Attempting to create a Device on that new device" << std::endl;
    Device *dev1 = new Device(newDeviceNumber);
    std::cout << "Created device " << *dev1 << std::endl;
    std::cout << "...and deleting it again" << std::endl;
    delete dev1;

    uint32_t ksockId;
    std::cout << "Looking for a replier to a message - there shouldn't be any" << std::endl;
    int rv = dev0.FindReplier(ksockId, "$.Fred");
    if (rv == 0)
        std::cout << "The call returns 0, so there is no replier - good" << std::endl;
    else
    {
        std::cout << "The call returns " << rv << " which is unexpected" << std::endl;
        return 1;
    }

    return 0;
}

int testKsock()
{
    Ksock k1;
    std::cout << k1 << std::endl;
    assert(!k1.IsOpen());

    int rv = k1.Open();
    assert(rv==0);
    assert(k1.IsOpen());
    std::cout << k1 << std::endl;
    assert(k1.Close() == 0);
    assert(!k1.IsOpen());
    std::cout << k1 << std::endl;

    Ksock sender(1);
    Ksock listener(1);
    Ksock replier(1);

    rv = sender.Open();
    assert(rv==0);
    rv = listener.Open();
    assert(rv==0);
    rv = replier.Open();
    assert(rv==0);

    rv = listener.Bind("$.Hello");
    assert(rv==0);
    rv = listener.Bind("$.Question");
    assert(rv==0);
    rv = replier.Bind("$.Question", true);
    assert(rv==0);

    MessageId msgId, tmpId;
    Message m1("$.Hello");
    rv = sender.Send(m1, &msgId);
    assert(rv==0);
    std::cout << m1 << (m1.IsEntire()?" entire":" pointy") << std::endl;
    std::cout << "Sent with message id " << msgId << std::endl;

    Message m2;
    rv = listener.Receive(m2);
    assert(rv == 0);
    assert(m2.GetName() == m2.GetName());
    assert(m2.GetMessageId(tmpId) == 0); assert(tmpId == msgId);
    assert(!m2.IsReply());
    assert(!m2.IsRequest());
    assert(!m2.WantsUsToReply());

    Message q1("$.Question");
    std::cout << q1 << (q1.IsEntire()?" entire":" pointy") << std::endl;
    rv = sender.SendRequest(q1, &msgId);
    assert(rv == 0);
    std::cout << q1 << (q1.IsEntire()?" entire":" pointy") << std::endl;
    std::cout << "Sent with message id " << msgId << std::endl;

    m2 = Message();
    rv = listener.Receive(m2);
    assert(rv == 0);
    assert(m2.GetName() == q1.GetName());
    assert(m2.GetMessageId(tmpId) == 0); assert(tmpId == msgId);
    assert(!m2.IsReply());
    assert(m2.IsRequest());
    assert(!m2.WantsUsToReply());

    Message rq1;
    rv = replier.Receive(rq1);
    assert(rv == 0);
    assert(rq1.GetName() == q1.GetName());
    assert(rq1.GetMessageId(tmpId) == 0); assert(tmpId == msgId);
    assert(!rq1.IsReply());
    assert(rq1.IsRequest());
    assert(rq1.WantsUsToReply());

    rv = replier.SendReply(m1, q1);     // Not marked "you must answer"
    assert(rv < 0);

    rv = replier.SendReply(m1, rq1);
    assert(rv == 0);
    assert(m1.IsReply());

    Message m3;
    rv = sender.Receive(m3);
    assert(rv == 0);
    assert(m1.GetName() == m3.GetName());
    assert(m3.IsReply());

    Message m4("$.Question");
    rv = sender.SendStatefulRequest(m4, m3, &msgId);
    assert(rv == 0);
    assert(m4.IsStatefulRequest());

    uint8_t data[] = {1,2,3,4};
    Message m5("$.Jim", data, 4);
    std::cout << m5 << std::endl;
    rv = replier.Receive(m5);
    assert(rv < 0);

    Message m6;
    rv = replier.Receive(m6);
    assert(rv == 0);
    std::cout << m6 << std::endl;
    assert(m6.IsRequest());
    assert(m6.IsStatefulRequest());

    rv = listener.Unbind("$.Question", true);
    assert(rv<0);
    assert(rv == -EINVAL);
    assert(rv == Error::MessageEINVAL);
    std::cout << Error::ToString(rv) << std::endl;
    rv = listener.Unbind("$.Question");
    assert(rv==0);
    rv = listener.Unbind("$.Hello");
    assert(rv==0);
    rv = replier.Unbind("$.Question", true);
    assert(rv==0);

    Ksock *k2 = new Ksock(1);
    std::cout << k2 << " " << *k2 << std::endl;
    delete k2;
    return 0;
}

int main()
{
    std::cout << "=== MessageId tests ===" << std::endl;
    if (testMessageIds())
    {
        std::cout << "Error testing MessageId code" << std::endl;
        return 1;
    }

    std::cout << "=== Message tests ===" << std::endl;
    if (testMessage())
    {
        std::cout << "Error testing Message code" << std::endl;
        return 1;
    }

    std::cout << "=== Device tests ===" << std::endl;
    if (testDevice())
    {
        std::cout << "Error testing Device code" << std::endl;
        return 1;
    }

    std::cout << "=== Ksock tests ===" << std::endl;
    if (testKsock())
    {
        std::cout << "Error testing Ksock code" << std::endl;
        return 1;
    }

    std::cout << "Green light: all tests passed" << std::endl;

    return 0;
}

// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
