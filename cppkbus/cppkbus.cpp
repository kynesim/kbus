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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cppkbus.h"
#include "kbus_defns.h"

namespace cppkbus
{

    // Write the message data to the file descriptor.
    // Cope with returned events that cause us to have to retry the
    // writing, so that the caller sees this as a single call.
    int SafeWrite(int mFd, const uint8_t *data, unsigned dataLen)
    {
        int rv;
        size_t countWritten = 0;

        for (;;)
        {
            rv = write(mFd, &data[countWritten], dataLen - countWritten);
            if (rv < 0)
            {
                if (errno != EINTR && errno != EAGAIN)
                    return -errno;      // not much else we can do
            }
            else
                countWritten += rv;

            if (countWritten == dataLen)
                break;
            else
            {
                // Wait until we're ready to write again.
                struct pollfd fds[1];
                fds[0].fd = mFd;
                fds[0].revents = 0;
                fds[0].events = POLLOUT;

                // Whatever poll() returns, try write() again - it will
                // return an appropriate error if something went badly
                // enough wrong.
                (void) poll(fds, 1, 1000);
            }
        }
        return 0;
    }

    // Similarly for reading message data
    int SafeRead(int mFd, uint8_t *data, unsigned dataLen)
    {
        int rv;
        size_t countRead = 0;

        for (;;)
        {
            rv = read(mFd, &data[countRead], dataLen - countRead);
            if (rv < 0)
            {
                if (errno != EINTR && errno != EAGAIN)
                    return -errno;      // not much else we can do
            }
            else
                countRead += rv;

            if (countRead == dataLen)
                break;
            else
            {
                // Wait until we're ready to read again.
                struct pollfd fds[1];
                fds[0].fd = mFd;
                fds[0].revents = 0;
                fds[0].events = POLLIN;

                // Whatever poll() returns, try read() again - it will
                // return an appropriate error if something went badly
                // enough wrong.
                (void) poll(fds, 1, 1000);
            }
        }
        return 0;
    }

    // Returns file descriptor for success, -errno for error
    int OpenKsockByNumber(unsigned devNum, unsigned flags)
    {
        int   rv;
        int   mask  = O_RDONLY | O_WRONLY | O_RDWR;
        char  format[] = "/dev/kbus%u";
        char  filename[strlen(format) + 11];

        sprintf(filename, format, devNum);

        rv = open(filename, flags & mask);
        if (rv < 0)
            return -errno;
        else
            return rv;
    }

    // Returns file descriptor for success, -errno for error
    int OpenKsockByName(const char *devName, unsigned flags)
    {
        int   rv;
        int   mask  = O_RDONLY | O_WRONLY | O_RDWR;

        rv = open(devName, flags & mask);
        if (rv < 0)
            return -errno;
        else
            return rv;
    }

    // Contants and so on =====================================================

    Constants::Constants() :
        kMessageNameReplierGone(KBUS_MSG_NAME_REPLIER_GONEAWAY),
        kMessageNameReplierIgnored(KBUS_MSG_NAME_REPLIER_IGNORED),
        kMessageNameReplierUnbound(KBUS_MSG_NAME_REPLIER_UNBOUND),
        kMessageNameReplierDisappeared(KBUS_MSG_NAME_REPLIER_DISAPPEARED),
        kMessageNameErrorSending(KBUS_MSG_NAME_ERROR_SENDING),
        kMessageNameUnbindEventsLost(KBUS_MSG_NAME_UNBIND_EVENTS_LOST),
        kMessageNameReplierBindEvent(KBUS_MSG_NAME_REPLIER_BIND_EVENT)
    { };

    const Constants& Constants::Get()
    {
        static Constants aC;

        return aC;
    }

    const std::string Error::ToString(const unsigned err)
    {
        return Error::ToString((const Enum) err);
    }

    const std::string Error::ToString(const Enum inEnum)
    {
        switch (inEnum)
        {
            case Error::MessageIsEmpty:
                return "Message is empty";
            case Error::MessageIsNotEmpty:
                return "Message is not empty";
            case Error::MessageHasNoId:
                return "Message has no id";
            case Error::DeviceHasNoName:
                return "Device has no name";
            case Error::DeviceModeUnset:
                return "Device mode (read/write) is not set";
            case Error::InvalidArguments:
                return "Invalid arguments";
            case Error::MessageNotInitialised:
                return "Message not initialised";

                // Then make some attempt to help with errno.h values
                // as used by KBUS

            case Error::MessageEADDRINUSE:
                return "EADDRINUSE: There is already a replier bound to this name";
            case Error::MessageEADDRNOTAVAIL:
                return "EADDRNOTAVAIL: No replier bound for this Request's name, or sender of Request has gone away";
            case Error::MessageEALREADY:
                return "EALREADY: Writing to Ksock, previous send has returned EALREADY";
            case Error::MessageEBADMSG:
                return "EBADMSG: The message name is not valid";
            case Error::MessageEBUSY:
                return "EBUSY: Replier's queue is full, or ALL_OR_FAIL and a recipient queue is full";
            case Error::MessageECONNREFUSED:
                return "ECONNREFUSED: Attempt to reply to wrong message or wrong Ksock";
            case Error::MessageEINVAL:
                return "EINVAL: Invalid argument";
            case Error::MessageEMSGSIZE:
                return "EMSGSIZE: Data was written after the final message end guard";
            case Error::MessageENAMETOOLONG:
                return "ENAMETOOLONG: The message name is too long";
            case Error::MessageENOENT:
                return "ENOENT: There is no such KBUS device";
            case Error::MessageENOLCK:
                return "ENOLCK: Cannor send request, sender has no room for a reply";
            case Error::MessageENOMSG:
                return "ENOMSG: Cannot send until a message has been written";
            case Error::MessageEPIPE:
                return "EPIPE: Cannot send to specific replier, they have unbound/gone away";
            case Error::MessageEFAULT:
                return "EFAULT: Internal KBUS error";
            case Error::MessageENOMEM:
                return "ENOMEM: Internal KBUS error, run out of memory";
            case Error::MessageEAGAIN:
                return "EAGAIN: Send with ALL_OR_WAIT and full target queue, or unbind and ReplierBindEvent cannot be sent";
            default:
                std::ostringstream os;
                os << "Unknown error " << (int)inEnum;
                return os.str();
        }
    }

    const std::string MessageFlags::ToString(const unsigned int inEnum)
    {
        std::ostringstream os;

        if (inEnum & MessageFlags::WantReply)
        {
            os << "REQ";
        }
        if (inEnum & MessageFlags::WantYouToReply)
        {
            if (os.tellp()) os << "|";
            os << "YOU";
        }
        if (inEnum & MessageFlags::Synthetic)
        {
            if (os.tellp()) os << "|";
            os << "SYN";
        }
        if (inEnum & MessageFlags::Urgent)
        {
            if (os.tellp()) os << "|";
            os << "URG";
        }
        if (inEnum & MessageFlags::AllOrWait)
        {
            if (os.tellp()) os << "|";
            os << "aWT";
        }
        if (inEnum & MessageFlags::AllOrFail)
        {
            if (os.tellp()) os << "|";
            os << "aFL";
        }
        return os.str();
    }

    // MessageId ==============================================================

    const std::string MessageId::ToString() const
    {
        std::ostringstream stream;
        stream << "[" << mNetworkId << "," << mSerialNum << "]";
        return stream.str();
    }

    // OrigFrom ===============================================================

    const std::string OrigFrom::ToString() const
    {
        std::ostringstream stream;
        stream << "[" << mNetworkId << "," << mLocalId << "]";
        return stream.str();
    }

    // Message ================================================================

#if 0
    // TODO: Do we need the copy constructor? I think not...
    Message::Message(const Message& other)
    {

        // TODO: If this message already has data, clear it out first
        // (but the <vector> is OK)
        //
        // TODO: There must be a better way of doing this

        std::cout << "** COPY " << std::hex << &other << " from " << std::hex << this << std::endl;

        mName = other.GetName();
        mIsEntire = true;       // to force the data to be copied

        SetData(other.GetData(), other.GetDataLength(), other.GetFlags());
    }

    // TODO: Do we need the operator= method? I think not...
    Message::Message& Message::operator=(const Message& other)
    {

        std::cout << "** ASGN " << std::hex << &other << " from " << std::hex << this << std::endl;

        if (this == &other)
            return *this;

        std::cout << "   (not the same object)" << std::endl;

        if (mIsEmpty && other.mIsEmpty)         // nothing to do
            return *this;

        if (other.mIsEmpty)
        {
            mIsEmpty = true;
            mPointyData = NULL;
            mPointyLen = 0;
            mData.resize(0);            // is this over-drastic?
        }

        // TODO: If this message already has data, clear it out first
        // (but the <vector> is OK)
        //
        // TODO: There must be a better way of doing this

        mName = other.GetName();
        mIsEntire = true;       // to force the data to be copied

        SetData(other.GetData(), other.GetDataLength(), other.GetFlags());

        return *this;
    }
#endif

    Message::Message(const char *inName)
    {
        if (inName)
            mName = std::string(inName, strlen(inName));
        SetData(NULL, 0, 0);
    }

    // Almost as simple as it gets
    Message::Message(const std::string& inName, const bool isRequest) :
        mName(inName)
    {
        SetData(NULL, 0, isRequest?MessageFlags::WantReply:0);
    }

    // Message from parts
    Message::Message(const std::string& inName,
            const uint8_t *data, const size_t nr_bytes,
            const uint32_t msgFlags, const bool copyData,
            const bool isRequest) :
        mIsEntire(copyData),
        mName(inName)
    {
        uint32_t    actualFlags = msgFlags;
        if (isRequest)
            actualFlags = msgFlags | KBUS_BIT_WANT_A_REPLY;
        else
            actualFlags = msgFlags & ~(KBUS_BIT_WANT_A_REPLY);

        SetData(data, nr_bytes, actualFlags);
    }

    // Message from many parts...
    Message::Message(const std::string& inName,
            const uint32_t msgFlags,
            const MessageId *id, const MessageId *inReplyTo,
            const uint32_t to, const uint32_t from,
            const OrigFrom *origFrom, const OrigFrom *finalTo,
            const uint8_t *data, const size_t nr_bytes, const bool copyData) :
        mIsEntire(copyData),
        mName(inName)
    {
        // It's still simplest to use the normal way to do this
        SetData(data, nr_bytes, msgFlags);

        // Even if we then have to re-extract this to finish off...
        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        if (id)
        {
            hdr->id.network_id = id->mNetworkId;
            hdr->id.serial_num = id->mSerialNum;
        }
        if (inReplyTo)
        {
            hdr->in_reply_to.network_id = inReplyTo->mNetworkId;
            hdr->in_reply_to.serial_num = inReplyTo->mSerialNum;
        }
        if (to != 0) hdr->to = to;
        if (from != 0) hdr->from = from;
        if (origFrom)
        {
            hdr->orig_from.network_id = origFrom->mNetworkId;
            hdr->orig_from.local_id   = origFrom->mLocalId;
        }
        if (finalTo)
        {
            hdr->final_to.network_id = finalTo->mNetworkId;
            hdr->final_to.local_id   = finalTo->mLocalId;
        }
    }

    int Message::BecomesReplyTo(const Message& inReplyTo)
    {
        if (mIsEmpty)
            return Error::MessageIsEmpty;

        if (!inReplyTo.WantsUsToReply())
            return -EBADMSG;

        struct kbus_message_header *this_hdr = (struct kbus_message_header *)&mData[0];
        struct kbus_message_header *that_hdr = (struct kbus_message_header *)&inReplyTo.mData[0];
        this_hdr->to          = that_hdr->from;
        this_hdr->in_reply_to = that_hdr->id;
        return 0;
    }

    int Message::BecomesStatefulRequest(const Message& earlierMessage)
    {
        if (mIsEmpty)
            return Error::MessageIsEmpty;

        struct kbus_message_header *this_hdr = (struct kbus_message_header *)&mData[0];
        struct kbus_message_header *that_hdr = (struct kbus_message_header *)&earlierMessage.mData[0];

        if (earlierMessage.IsReply())
        {
            this_hdr->final_to = that_hdr->orig_from;
            this_hdr->to       = that_hdr->from;
            this_hdr->flags   |= KBUS_BIT_WANT_A_REPLY;
        }
        else if (earlierMessage.IsStatefulRequest())
        {
            this_hdr->final_to = that_hdr->final_to;
            this_hdr->to       = that_hdr->to;
            this_hdr->flags   |= KBUS_BIT_WANT_A_REPLY;
        }
        else
            return -EBADMSG;

        return 0;
    }

    // Sort out the message contents
    // Assumes that the object already knows (a) its name and (b) whether or
    // not it is "pointy"
    void Message::SetData(const uint8_t *inData, const uint32_t inDataLen,
            const uint32_t msgFlags)
    {
        struct kbus_message_header *hdr = NULL;
        std::vector<uint8_t>::size_type sizeWanted = 0;

        if (mIsEntire)
        {
            sizeWanted = KBUS_ENTIRE_MSG_LEN(mName.size(), inDataLen);
        }
        else
        {
            // Since we're "pointy", we only need room for the message header
            sizeWanted = sizeof(struct kbus_message_header);
        }

        // Make it the size we want (now)
        mData.resize(sizeWanted);

        hdr = (struct kbus_message_header *)&mData[0];

        // But it's only the message header we need to zero...
        memset(hdr, 0, sizeof(*hdr));

        hdr->start_guard = KBUS_MSG_START_GUARD;
        hdr->flags      = msgFlags;
        hdr->name_len   = mName.size();
        hdr->data_len   = inDataLen;
        hdr->end_guard = KBUS_MSG_END_GUARD;

        if (mIsEntire)
        {
            int data_index = KBUS_ENTIRE_MSG_DATA_INDEX(hdr->name_len);
            int end_guard_index = KBUS_ENTIRE_MSG_END_GUARD_INDEX(hdr->name_len,
                    hdr->data_len);

            struct kbus_entire_message *buf = (struct kbus_entire_message *)hdr;

            // We would really quite like to leave the message name zero
            // terminated - it's nicer for anyone debugging, for a start.
            // It should be enough to zero the last 32-bit word.
            unsigned name_len_in_words = (KBUS_PADDED_NAME_LEN(hdr->name_len)/4);
            buf->rest[name_len_in_words] = 0;

            memcpy(&buf->rest[0],  mName.c_str(), hdr->name_len);
            memcpy(&buf->rest[data_index], inData, inDataLen);

            buf->rest[end_guard_index] = KBUS_MSG_END_GUARD;
        }
        else
        {
            // Do we ever need these? Well, they're useful if we're
            // ever asked to return them, without needing to reconstruct
            // them from the header data
            mPointyData = inData;
            mPointyLen = inDataLen;

            // This is not really a very good idea - could we convince
            // the KBUS maintainers to make the KBUS values const?
            hdr->data = (void *)inData;
            hdr->name = (char *)mName.c_str(); // this won't change if we don't mutate mName
        }
        mIsEmpty = false;
    }

    int Message::SetFlags(const uint32_t newFlags)
    {
        if (mIsEmpty) return -1;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        hdr->flags = newFlags;
        return 0;
    }

    uint32_t Message::GetFlags() const
    {
        if (mIsEmpty) return 0;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        return hdr->flags;
    }


    bool Message::WantsUsToReply() const
    {
        if (mIsEmpty) return false;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        return (hdr->flags & KBUS_BIT_WANT_A_REPLY) &&
            (hdr->flags & KBUS_BIT_WANT_YOU_TO_REPLY);
    }

    bool Message::IsRequest() const
    {
        if (mIsEmpty) return false;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        return (hdr->flags & KBUS_BIT_WANT_A_REPLY) != 0;
    }

    bool Message::IsStatefulRequest() const
    {
        if (mIsEmpty) return false;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        return (hdr->flags & KBUS_BIT_WANT_A_REPLY) && (hdr->to != 0);
    }

    bool Message::IsReply() const
    {
        if (mIsEmpty) return false;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        return hdr->in_reply_to.network_id != 0 ||
            hdr->in_reply_to.serial_num != 0;
    }

    bool Message::IsReplierBindEvent() const
    {
        if (mIsEmpty) return false;

        return (!mName.compare(0, strlen(KBUS_MSG_NAME_REPLIER_BIND_EVENT),
                    KBUS_MSG_NAME_REPLIER_BIND_EVENT));

    }

    int Message::GetReplierBindEventData(bool& isBind, uint32_t& binder,
            std::string& messageName) const
    {
        if (mIsEmpty || !IsReplierBindEvent()) return -1;

        const uint8_t *data = GetData();
        struct kbus_replier_bind_event_data  *event_data;

        event_data = (struct kbus_replier_bind_event_data *)data;

        isBind = event_data->is_bind;
        messageName = std::string((char *)event_data->rest, event_data->name_len);
        binder = event_data->binder;
        return 0;
    }

    const uint8_t *Message::GetData() const
    {
        if (mIsEmpty) return NULL;

        if (GetDataLength() == 0)
            return NULL;
        else if (mIsEntire)
        {
            struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
            return (uint8_t *) kbus_msg_data_ptr(hdr);
        }
        else
            return mPointyData;
    }

    size_t Message::GetDataLength() const
    {
        if (mIsEmpty) return 0;

        if (mIsEntire)
        {
            struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
            return hdr->data_len;
        }
        else
            return mPointyLen;
    }

    int Message::GetMessageId(MessageId& outMessageId) const
    {
        if (mIsEmpty) return -1;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];

        outMessageId.mNetworkId = hdr->id.network_id;
        outMessageId.mSerialNum = hdr->id.serial_num;
        return 0;
    }

    int Message::GetInReplyTo(MessageId& outMessageId) const
    {
        if (mIsEmpty) return -1;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];

        outMessageId.mNetworkId = hdr->in_reply_to.network_id;
        outMessageId.mSerialNum = hdr->in_reply_to.serial_num;
        return 0;
    }

    int Message::GetTo(uint32_t& outKsockId) const
    {
        if (mIsEmpty) return -1;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        outKsockId = hdr->to;
        return 0;
    }

    int Message::GetFrom(uint32_t& outKsockId) const
    {
        if (mIsEmpty) return -1;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];
        outKsockId = hdr->from;
        return 0;
    }

    int Message::GetOrigFrom(OrigFrom& outOrigFrom) const
    {
        if (mIsEmpty) return -1;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];

        outOrigFrom.mNetworkId = hdr->orig_from.network_id;
        outOrigFrom.mLocalId   = hdr->orig_from.local_id;
        return 0;
    }

    int Message::GetFinalTo(OrigFrom& outFinalTo) const
    {
        if (mIsEmpty) return -1;

        struct kbus_message_header *hdr = (struct kbus_message_header *)&mData[0];

        outFinalTo.mNetworkId = hdr->final_to.network_id;
        outFinalTo.mLocalId   = hdr->final_to.local_id;
        return 0;
    }

    std::string Message::ToString() const
    {
        bool isBindEvent = false;
        std::ostringstream stream;

        stream << "<";

        if (mIsEmpty)
        {
            stream << "EmptyMessage>";
            return stream.str();
        }

        if (IsReply())
        {
            if (!mName.compare(0, 7, "$.KBUS"))
                stream << "Status";
            else
                stream << "Reply";
        }
        else if (IsRequest())
            stream << "Request";
        else if (IsReplierBindEvent())
        {
            stream << "ReplierBindEvent";
            isBindEvent = true;
        }
        else
            stream << "Message";        // Hmm, or "Announcement"

        if (!isBindEvent)
            stream << " \"" << mName << '"';

        kbus_message_header *hdr = (kbus_message_header *)&mData[0];

        if (hdr->id.network_id != 0 || hdr->id.serial_num != 0)
            stream << " id=[" << hdr->id.network_id << "," << hdr->id.serial_num << "]";

        if (hdr->to)
            stream << " to=" << hdr->to;

        if (hdr->from)
            stream << " from=" << hdr->from;

        if (hdr->orig_from.network_id != 0 || hdr->orig_from.local_id != 0)
            stream << " orig_from=[" << hdr->orig_from.network_id << ","
                << hdr->orig_from.local_id << "]";

        if (hdr->final_to.network_id != 0 || hdr->final_to.local_id != 0)
            stream << " final_to=[" << hdr->final_to.network_id << ","
                << hdr->final_to.local_id << "]";

        if (hdr->in_reply_to.network_id != 0 || hdr->in_reply_to.serial_num != 0)
            stream << " in_reply_to=[" << hdr->in_reply_to.network_id << ","
                << hdr->in_reply_to.serial_num << "]";

        if (hdr->flags)
        {
            stream << " flags=" << std::hex << hdr->flags;
            stream << ' ' << MessageFlags::ToString(hdr->flags);
        }

        if (hdr->data_len > 0)
        {
            if (isBindEvent)
            {
                bool        isBind;
                uint32_t    binder;
                std::string msgName;
                (void) GetReplierBindEventData(isBind, binder, msgName);
                stream << " [" << (isBind?"Bind \"":"Unbind \"") << msgName <<
                    "\" for " << binder << "]";
            }
            else
            {
                const uint8_t *data = GetData();
                int ii, minlen = hdr->data_len<20?hdr->data_len:20;
                stream << " data=\"";
                for (ii=0; ii<minlen; ii++)
                {
                    char  ch = data[ii];
                    if (isprint(ch))
                        stream << ch;
                    else
                        stream << "\\x" << std::hex << (unsigned) ch;
                }
                if (hdr->data_len > 20)
                    stream << "...";
                stream << '"';
            }
        }
        stream << ">";
        return stream.str();
    }

    // Device =================================================================

    Device::Device& Device::operator=(const Device& other)
    {
        if (mFd >= 0)
        {
            close(mFd);
            mFd = -1;
        }
        mDeviceNumber = other.mDeviceNumber;
        mDeviceName = other.mDeviceName;
        mDeviceMode = other.mDeviceMode;
        return *this;
    }

    Device::Device(const unsigned inDeviceNumber, std::ios::openmode inMode) :
        mDeviceNumber(inDeviceNumber),
        mDeviceMode(inMode),
        mFd(-1)
    {
        std::ostringstream tmpstr;
        tmpstr << "/dev/kbus" << inDeviceNumber;
        mDeviceName = tmpstr.str();
    }

    Device::~Device()
    {
        if (mFd >= 0) { close(mFd); mFd = -1; }
    }

    // Returns 0 for success, -errno for error
    int Device::EnsureOpen() const
    {
        int rv;
        if (mFd >= 0)
            return 0;

        if (mDeviceName.length())
        {
            unsigned mode = 0;
            if (mDeviceMode & (std::ios::in | std::ios::out))
                mode = O_RDWR;
            else if (mDeviceMode & std::ios::in)
                mode = O_WRONLY;
            else if (mDeviceMode & std::ios::out)
                mode = O_RDONLY;
            else
                return Error::DeviceModeUnset;

            rv = OpenKsockByName(mDeviceName.c_str(), mode);
        }
        else
        {
            mFd = -1;
            return Error::DeviceHasNoName;
        }

        if (rv < 0) {
            mFd = -1;
            return rv;
        }
        else
        {
            mFd = rv;
            return 0;
        }
    }

    // *Maybe* close our device. Define this now so that we can
    // change our mind about behaviour in one place, later on.
    void Device::MaybeClose() const
    {
        // In fact, at the moment, never do so...
    }

    int Device::Close() const
    {
        if (mFd == -1)
            return 0;
        else
        {
            int rv = close(mFd);
            mFd = -1;               // regardless
            if (rv < 0)
                return -errno;
            else
                return 0;
        }
    }

    int Device::MakeKernelVerbose(const bool inVerbose)
    {
        uint32_t array[1];

        int rv = this->EnsureOpen();
        if (rv) return rv;

        array[0] = (inVerbose ? 1 : 0);
        rv = ::ioctl(mFd, KBUS_IOC_VERBOSE, array);
        if (rv < 0)
            rv = -errno;

        this->MaybeClose();
        return rv;
    }

    int Device::QueryKernelVerbose(bool& outVerbose) const
    {
        uint32_t array[1];

        int rv = this->EnsureOpen();
        if (rv) return rv;

        array[0] = 0xFFFFFFFF;
        rv = ::ioctl(mFd, KBUS_IOC_VERBOSE, array);
        outVerbose = (array[0] != 0);
        if (rv < 0)
            rv = -errno;

        this->MaybeClose();
        return rv;
    }

    int Device::ReportReplierBinds(const bool shouldReport)
    {
        uint32_t array[1];

        int rv = this->EnsureOpen();
        if (rv) return rv;

        array[0] = (shouldReport ? 1 : 0);
        rv = ::ioctl(mFd, KBUS_IOC_REPORTREPLIERBINDS, array);
        if (rv < 0)
            rv = -errno;

        this->MaybeClose();
        return rv;
    }

    int Device::ReportingReplierBinds(bool& areReporting) const
    {
        uint32_t array[1];

        int rv = this->EnsureOpen();
        if (rv) return rv;

        array[0] = 0xFFFFFFFF;
        rv = ::ioctl(mFd, KBUS_IOC_REPORTREPLIERBINDS, &array);
        areReporting = (array[0] != 0);
        if (rv < 0)
            rv = -errno;

        this->MaybeClose();
        return rv;
    }

    int Device::FindReplier(uint32_t &outKsockId, const std::string& inMessageName)
    {
        struct kbus_bind_query query;

        int rv = this->EnsureOpen();
        if (rv) return rv;

        query.name = (char *)inMessageName.c_str();
        query.name_len = strlen(query.name);

        rv = ioctl(this->mFd, KBUS_IOC_REPLIER, &query);
        if (rv < 0)
            rv = -errno;
        else if (rv == 0)
        {
            outKsockId = 0;
        }
        else
        {
            outKsockId = query.return_id;
            rv = 1;
        }
        this->MaybeClose();
        return rv;
    }

    int Device::AddNewDevice(unsigned& outNumber)
    {
        uint32_t  newDevNum;
        int rv = this->EnsureOpen();
        if (rv) return rv;

        rv = ioctl(mFd, KBUS_IOC_NEWDEVICE, &newDevNum);
        if (rv < 0)
            rv = -errno;
        else
            outNumber = newDevNum;

        this->MaybeClose();
        return rv;
    }

    const std::string Device::ToString(bool inner) const
    {
        std::ostringstream stream;
        if (inner)
            stream << "device ";
        else
            stream << "<Device ";
        if (mDeviceNumber != -1)
            stream << mDeviceNumber;
        if (mDeviceNumber != -1 && mDeviceName.length())
            stream << ", ";
        if (mDeviceName.length())
            stream << '"' << mDeviceName << '"';
        if (mFd != -1)
        {
            stream << " open for ";
            if (mDeviceMode & (std::ios::in | std::ios::out))
                stream << "read/write";
            else if (mDeviceMode & std::ios::in)
                stream << "write";
            else if (mDeviceMode & std::ios::out)
                stream << "read";
            else
                stream << std::hex << mDeviceMode;
        }
        if (!inner)
            stream << ">";
        return stream.str();
    }

    // Ksock ==================================================================

    Ksock::~Ksock()
    {
        this->Close();
    }

    int Ksock::Open()
    {
        return mDevice.EnsureOpen();
    }

    int Ksock::Close()
    {
        return mDevice.Close();
    }

    int Ksock::Bind(const std::string& inName, bool asReplier)
    {
        int rv;
        struct kbus_bind_request bind_rq;

        bind_rq.is_replier = asReplier?1:0;
        bind_rq.name_len = inName.length();
        bind_rq.name = (char *)inName.c_str();

        rv = ioctl(mDevice.mFd, KBUS_IOC_BIND, &bind_rq);
        if (rv < 0)
            return -errno;
        else
            return 0;
    }

    int Ksock::Unbind(const std::string& inName, bool asReplier)
    {
        int rv;
        struct kbus_bind_request bind_rq;

        bind_rq.is_replier = asReplier?1:0;
        bind_rq.name_len = inName.length();
        bind_rq.name = (char *)inName.c_str();

        rv = ioctl(mDevice.mFd, KBUS_IOC_UNBIND, &bind_rq);
        if (rv < 0)
            return -errno;
        else
            return 0;
    }

    int Ksock::GetId(uint32_t &outId) const
    {
        int rv = ioctl(mDevice.mFd, KBUS_IOC_KSOCKID, &outId);
        if (rv < 0)
            return -errno;
        else
            return 0;
    }

    int Ksock::GetLastMessageId(MessageId& outMessageId) const
    {
        struct kbus_msg_id msg_id;
        int rv = ioctl(mDevice.mFd, KBUS_IOC_LASTSENT, &msg_id);
        if (rv < 0)
        {
            return -errno;
        }
        else
        {
            outMessageId.mNetworkId = msg_id.network_id;
            outMessageId.mSerialNum = msg_id.serial_num;
            return 0;
        }
    }

    int Ksock::SetMaxUnreadMessages(const uint32_t qlen)
    {
        int rv = ioctl(mDevice.mFd, KBUS_IOC_MAXMSGS, &qlen);
        if (rv < 0)
            return -errno;
        else
            return 0;
    }

    int Ksock::GetMaxUnreadMessages(uint32_t& qlen) const
    {
        // We do it by trying to set the number to 0
        uint32_t nr_msgs = 0;
        int rv = ioctl(mDevice.mFd, KBUS_IOC_MAXMSGS, &nr_msgs);
        if (rv < 0)
        {
            return -errno;
        }
        else
        {
            qlen = nr_msgs;
            return 0;
        }
    }

    int Ksock::HowManyMessagesUnread(int& outNum) const
    {
        int rv;
        uint32_t nr_msgs = 0;

        rv = ioctl(mDevice.mFd, KBUS_IOC_NUMMSGS, &nr_msgs);
        if (rv < 0)
        {
            return -errno;
        }
        else
        {
            outNum = nr_msgs;
            return 0;
        }
    }

    int Ksock::HowManyMessagesUnrepliedTo(int& outNum) const
    {
        int rv;
        uint32_t nr_msgs = 0;

        rv = ioctl(mDevice.mFd, KBUS_IOC_UNREPLIEDTO, &nr_msgs);
        if (rv < 0)
        {
            return -errno;
        }
        else
        {
            outNum = nr_msgs;
            return 0;
        }
    }

    int Ksock::ReceiveMessagesOnlyOnce(const bool shouldReceiveOnce)
    {
        uint32_t array[1] = { shouldReceiveOnce?1:0 };
        int rv = ioctl(mDevice.mFd, KBUS_IOC_MSGONLYONCE, &array[0]);
        if (rv < 0)
            return -errno;
        else
            return 0;
    }

    int Ksock::WillReceiveOnlyOnce(bool& onlyOnce) const
    {
        uint32_t array[1] = { 0xFFFFFFFF };
        int rv = ioctl(mDevice.mFd, KBUS_IOC_MSGONLYONCE, &array[0]);
        if (rv < 0)
        {
            return -errno;
        }
        else
        {
            onlyOnce = array[0] ? true : false;
            return 0;
        }
    }

    int Ksock::Send(Message& ioMessage, MessageId *msgId)
    {
        if (ioMessage.IsEmpty())
        {
            return Error::MessageNotInitialised;
        }

        kbus_message_header *hdr = (kbus_message_header *)(&ioMessage.mData[0]);
        int msgLen = ioMessage.mData.size();    // we hope/trust this is the right length

        int rv = SafeWrite(mDevice.mFd, (uint8_t *)hdr, msgLen);
        if (rv < 0) return -errno;

        struct kbus_msg_id id;

        rv = ioctl(mDevice.mFd, KBUS_IOC_SEND, &id);
        if (rv < 0) return -errno;

        if (msgId) {
            msgId->mNetworkId = id.network_id;
            msgId->mSerialNum = id.serial_num;
        }
        return 0;
    }

    int Ksock::SendRequest(Message& ioMessage, MessageId *msgId)
    {
        if (ioMessage.IsEmpty())
        {
            return Error::MessageNotInitialised;
        }

        uint32_t flags = ioMessage.GetFlags();
        (void) ioMessage.SetFlags( flags | KBUS_BIT_WANT_A_REPLY);

        return Send(ioMessage, msgId);
    }

    int Ksock::SendReply(Message& ioMessage, const Message& inReplyTo, MessageId *msgId)
    {
        if (ioMessage.IsEmpty())
        {
            return Error::MessageNotInitialised;
        }

        int rv = ioMessage.BecomesReplyTo(inReplyTo);
        if (rv < 0) return rv;

        return Send(ioMessage, msgId);
    }

    int Ksock::SendStatefulRequest(Message& ioMessage, const Message& earlierMessage,
            MessageId *msgId)
    {
        if (ioMessage.IsEmpty())
        {
            return Error::MessageNotInitialised;
        }

        int rv = ioMessage.BecomesStatefulRequest(earlierMessage);
        if (rv < 0) return rv;

        return Send(ioMessage, msgId);
    }

    int Ksock::Receive(Message& ioMessage)
    {
        uint32_t msgLen;

        if (!ioMessage.IsEmpty())
            return Error::MessageIsNotEmpty;

        int rv = ioctl(mDevice.mFd, KBUS_IOC_NEXTMSG, &msgLen);
        if (rv < 0) return -errno;

        ioMessage.mData.resize(msgLen);
        rv = SafeRead(mDevice.mFd, (uint8_t *)(&ioMessage.mData[0]), msgLen);
        if (rv  < 0) return rv;

        kbus_message_header *hdr = (kbus_message_header *)(&ioMessage.mData[0]);

        ioMessage.mName = std::string(kbus_msg_name_ptr(hdr), hdr->name_len);
        ioMessage.mIsEntire = true;
        ioMessage.mIsEmpty = false;
        ioMessage.mPointyData = NULL;
        ioMessage.mPointyLen = 0;
        return 0;
    }

    int Ksock::WaitForMessage(unsigned int &outPollFlags,
            const unsigned int inPollFlags, const int timeout)
    {
        struct pollfd fds[1];
        int rv;


        fds[0].fd = mDevice.mFd;
        fds[0].revents = 0;
        fds[0].events = ((inPollFlags & PollFlags::Receive) ? POLLIN : 0) |
            ((inPollFlags & PollFlags::Transmit) ? POLLOUT : 0);
        rv = poll(fds, 1, timeout);
        if (rv > 0)
        {
            outPollFlags =
                ((fds[0].revents & POLLIN) ? PollFlags::Receive : 0) |
                ((fds[0].revents & POLLOUT) ? PollFlags::Transmit : 0);
        }
        else
        {
            outPollFlags = 0;
        }
        return ((rv < 0) && errno != EINTR && errno != EAGAIN) ? -1 : rv;
    }

    const std::string Ksock::ToString() const
    {
        std::ostringstream stream;
        stream << "<Ksock ";
        if (IsOpen())
        {
            uint32_t id;
            int rv = GetId(id);
            if (rv < 0)
                stream << "?? ";
            else
                stream << id << " ";
        }
        stream << "on ";
        stream << mDevice.ToString(true);
        stream << ">";
        return stream.str();
    }
}

// OPERATORS ==============================================================

std::ostream& operator<<(std::ostream& stream, const cppkbus::Error::Enum inEnum) {
    stream << cppkbus::Error::ToString(inEnum);
    return stream;
}

std::ostream& operator<<(std::ostream& stream, const cppkbus::Device& device) {
    stream << device.ToString();
    return stream;
}

std::ostream& operator<<(std::ostream& stream, const cppkbus::MessageId& id) {
    stream << id.ToString();
    return stream;
}

std::ostream& operator<<(std::ostream& stream, const cppkbus::OrigFrom& origFrom) {
    stream << origFrom.ToString();
    return stream;
}

std::ostream& operator<<(std::ostream& stream, const cppkbus::Message& msg) {
    stream << msg.ToString();
    return stream;
}

std::ostream& operator<<(std::ostream& stream, const cppkbus::Ksock& ksock) {
    stream << ksock.ToString();
    return stream;
}

/* End file */

// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
