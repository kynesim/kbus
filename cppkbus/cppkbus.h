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

#ifndef _CPPKBUS_H_INCLUDED_
#define _CPPKBUS_H_INCLUDED_

/** A C++ interface to KBUS.
 *
 * This is an exceptionless API.
 *
 * Exceptions and RTTI are not used in this library, as we know that we have
 * potential users who need to be able to compile with::
 *
 *     -fno-exceptions -fno-rtti
 *
 * in an attempt to optimise their code (and particularly to keep code size down).
 */

// NB: It is intended that end-users should be able to use this file without
// needing to include the KBUS module 'C' header file. Thus all kbus_defns.h
// reliances are in cppkbus.cpp.

#include <stdint.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#include <limits.h>

namespace cppkbus
{
    /**
     * Some useful constants (taken from kbus_defns.h)
     *
     * Use as, for instance::
     *
     *      static Constants c = Constants::Get();
     *      std::string name;
     *      int rv = msg.GetName(name);
     *      if (rv < 0) ...
     *      if (name == c.kMessageNameReplierBindEvent) ...
     *
     */

    class Constants
    {
        public:
            Constants();

            /*
             * These are sent to the original Sender of a Request when KBUS knows that the
             * Replier is not going to Reply. In all cases, you can identify which message
             * they concern by looking at the "inReplyTo" field:
             */

            //! Synthetic message: Replier has gone away before reading the request.
            const std::string kMessageNameReplierGone;

            //! Synthetic message: replier went away after reading the request
            //! but before replying to it.
            const std::string kMessageNameReplierIgnored;

            //! Synthetic message: Replier unbound from the message queue
            //!  whilst processing your message (and will therefore never reply)
            const std::string kMessageNameReplierUnbound;

            //! Synthetic message: Replier disappeared; typically the ksock
            //!  bound as your replier was closed.
            const std::string kMessageNameReplierDisappeared;

            //! Couldn't send a request.
            const std::string kMessageNameErrorSending;

            /* Synthetic Announcements with no data  */

            /**
             * * UnbindEventsLost: Sent (instead of a Replier Bind Event) when the unbind
             *   events "set aside" list has filled up, and thus unbind events have been
             *   lost.
             */
            const std::string kMessageNameUnbindEventsLost;

            //! Replier Bind Event
            const std::string kMessageNameReplierBindEvent;

            /** Retrieve the constants structure */
            static const Constants& Get();
    };


    /*
     * Some errors. KBUS itself uses errno,h values (sometimes hijacked rather
     * from their original meaning). We need some extra error codes for our own
     * purposes.
     *
     * In order to try to avoid clashing with (the errno values we care about),
     * we shall start our own values at the top of 'int' space...
     */
    namespace Error
    {
        typedef enum
        {
            MessageIsEmpty = -(INT_MAX - 1),

            MessageIsNotEmpty = -(INT_MAX - 2),

            MessageHasNoId = -(INT_MAX - 3),

            //! Attempt to open a device with an empty name.
            DeviceHasNoName = -(INT_MAX - 4),

            // Device mode does not contain ios::in or ios::out flags
            DeviceModeUnset = -(INT_MAX - 5),

            //! Invalid arguments
            InvalidArguments = -(INT_MAX - 6),

            //! Attempt to send an uninitialised message.
            MessageNotInitialized = -(INT_MAX - 7),
            MessageNotInitialised = MessageNotInitialized,

            MessageEADDRINUSE = -EADDRINUSE,
            MessageEADDRNOTAVAIL = -EADDRNOTAVAIL,
            MessageEALREADY = -EALREADY,
            MessageEBADMSG = -EBADMSG,
            MessageEBUSY = -EBUSY,
            MessageECONNREFUSED = -ECONNREFUSED,
            MessageEINVAL = -EINVAL,
            MessageEMSGSIZE = -EMSGSIZE,
            MessageENAMETOOLONG = -ENAMETOOLONG,
            MessageENOENT = -ENOENT,
            MessageENOLCK = -ENOLCK,
            MessageENOMSG = -ENOMSG,
            MessageEPIPE = -EPIPE,
            MessageEFAULT = -EFAULT,
            MessageENOMEM = -ENOMEM,
            MessageEAGAIN = -EAGAIN,
        } Enum;

        const std::string ToString(const unsigned err);
        const std::string ToString(const Enum inEnum);
    };


    /** Inherit from here to suppress copying */
    class NoCopy
    {
        public:
            NoCopy() { };
        private:
            // DELIBERATELY UNIMPLEMENTED, thus disabling copying

            NoCopy(const NoCopy& other);
            NoCopy& operator=(const NoCopy& other);
    };

    namespace OpenFlags
    {
        static const unsigned Read    = 1;
        static const unsigned Write   = 2;
        static const unsigned OpenNow = 4;

        const std::string ToString(const unsigned inOpenFlags);
    };

    namespace MessageFlags
    {
        //! This message requires a reply.
        static const uint32_t WantReply = (1<<0);

        //! This message was received by you and you are to reply
        static const uint32_t WantYouToReply = (1<<1);

        //! Synthetic message
        static const uint32_t Synthetic = (1<<2);

        //! Urgent
        static const uint32_t Urgent = (1<<3);

        //! All or wait - send all messages before returning
        static const uint32_t AllOrWait = (1<<8);

        //! All or fail - fail if not all messages could be sent now
        static const uint32_t AllOrFail = (1<<9);

        const std::string ToString(const uint32_t inMessageFlags);
    };

    namespace BindFlags
    {

        //! Be a replier for the given event.
        static const unsigned Replier = (1<<1);

        //! Convenience element.
        static const unsigned Listener = (1<<2);

        const std::string ToString(const unsigned inBindFlags);
    };

    namespace PollFlags
    {
        //! receive
        static const unsigned Receive = (1<<0);

        //! Transmit
        static const unsigned Transmit = (1<<1);

        //! An error
        static const unsigned Error = (1<<2);

        const std::string ToString(const unsigned inPollFlags);
    };


    // Forward decl ..
    //class Ksock;


    /**
     * Represents a message id
     *
     * The network id is normally only used in the context of Limpets, and is
     * otherwise normally 0. However, KBUS itself will not change it.
     *
     * The serial number is assigned by KBUS when it sends a message (so after
     * the message has been written into the kernel module).
     *
     * When creating a new message (for sending), both fields should be set to
     * 0.
     */
    class MessageId
    {
        public:
            uint32_t mNetworkId;
            uint32_t mSerialNum;

            MessageId() :
                mNetworkId(0), mSerialNum(0) { }

            MessageId(const uint32_t inNetwork,
                    const uint32_t inSerial) :
                mNetworkId(inNetwork), mSerialNum(inSerial) { };

            int compare(const MessageId& other) const
            {
                if (this->mNetworkId < other.mNetworkId) { return -1; }
                if (this->mNetworkId > other.mNetworkId) { return 1; }
                if (this->mSerialNum < other.mSerialNum) { return -1; }
                if (this->mSerialNum > other.mSerialNum) { return 1; }
                return 0;
            }

            bool operator==(const MessageId& other)  const
            {
                return !this->compare(other);
            }

            bool operator<(const MessageId& other) const
            {
                return this->compare(other) < 0;
            }

            bool operator>(const MessageId& other) const
            {
                return this->compare(other) > 0;
            }

            const std::string ToString() const;
    };

    /**
     * Represents a location on the other side of a Limpet
     *
     * Despite the name (which comes directly from the kbus_defns.h header, for
     * compatibility), it is used for both OrigFrom and FinalTo fields.
     *
     * Values of {0,0} indicate "unset".
     */
    class OrigFrom
    {
        public:
            uint32_t mNetworkId;
            uint32_t mLocalId;

            OrigFrom() : mNetworkId(0), mLocalId(0) { };
            OrigFrom(const uint32_t inNetworkId, const uint32_t inLocalId) :
                mNetworkId(inNetworkId), mLocalId(inLocalId) { }

            int compare(const OrigFrom& other) const
            {
                if (this->mNetworkId < other.mNetworkId) { return -1; }
                if (this->mNetworkId > other.mNetworkId) { return 1; }
                if (this->mLocalId < other.mLocalId) { return -1; }
                if (this->mLocalId > other.mLocalId) { return 1; }
                return 0;
            }

            bool operator==(const OrigFrom& other)  const
            {
                return !this->compare(other);
            }

            bool operator<(const OrigFrom& other) const
            {
                return this->compare(other) < 0;
            }

            bool operator>(const OrigFrom& other) const
            {
                return this->compare(other) > 0;
            }

            const std::string ToString() const;
    };


    /**
     *
     * This class represents a KBUS message. The details of how KBUS messages
     * work may be found in the kbus_defns.h file and the documentation.
     *
     * The default contructor produces an "empty" message, suitable for passing
     * to the Ksock 'receive()' method, so that it can fill it with the
     * appropriate content. Thus one can do::
     *
     *     Message  msg;
     *     int rv = ksock.Receive(msg);
     *
     * with the expected effect.
     *
     * The non-default constructors are used to build a new message for sending.
     *
     * Note that there is no support for changing a message's name or data once
     * it has been set.
     */
    class Message
    {
        public:
            /*! Create an empty/unset message, suitable for 'Receive()'ing into,
             * or for copying an existing message into.
             */
            Message() : mIsEmpty(true) { }

            // In case anyone subclasses from us
            virtual ~Message() { }

#if 0
            // I believe the default behaviour is sufficient
            Message(const Message& other);
            Message& operator=(const Message& other);
#endif

            /**
             * A bare-bones constructor for the simplest possible (proper) message
             */
            explicit Message(const std::string& inName) : mName(inName) { }

            /*
             * And one for a basic char* (obviously a *bit* dangerous).
             *
             * The 'explicit' stops people doing::
             *
             *      Message m = "$.String";
             *
             * which we probably don't want to allow...
             */
            explicit Message(const char *inName);

            /**
             * Our simplest-possible real message might be a request...
             */
            Message(const std::string& inName, const bool isRequest=false);

            /**
             * Create a message from its parts.
             *
             * If copyData is false, then this will be a "pointy" message (and the data
             * must therefore stay around until this message has been finished with).
             *
             * If copyData is true, then the data will be copied and you may do what
             * you like with the original name and data once the constructor has
             * returned.
             *
             * If isRequest is true, then the WantReply bit will be set in the
             * message flags, regardless of any value pass in msgFlags.
             */
            Message(const std::string& inName,
                    const uint8_t *data=NULL, const size_t nr_bytes=0,
                    const uint32_t msgFlags=0, const bool copyData=true,
                    const bool isRequest=false);

            /*
             * Sometimes it is useful to be able to specify *all* the details of a
             * message (probably not in normal use, but certainly in testing.)
             *
             * Obviously take great care using this...
             *
             * If copyData is true then data will be copied, giving an "entire"
             * message, otherwise it will not, giving a "pointy" message. In the latter
             * case, do not free data until the message is no longer using it.
             *
             * Regardless of the value of copyData, all of the other parameters are
             * always copied.
             *
             * NULL values mean 'use the unset value of'.
             */
            Message(const std::string& inName, const uint32_t msgFlags=0,
                    const MessageId *id=NULL, const MessageId *inReplyTo=NULL,
                    const uint32_t to=0, const uint32_t from=0,
                    const OrigFrom *origFrom=NULL, const OrigFrom *finalTo=NULL,
                    const uint8_t *data=NULL, const size_t nr_bytes=0, const bool copyData=true);

            /*
             * Make this message a Reply to another (earlier) message
             *
             * It does this by:
             *
             * * setting this messages 'to' field to inReplyTo's 'from' field
             * * setting this messages 'in_reply_to' field to inReplyTo's 'id'
             *
             * Note that the KBUS documentation assumes that a Reply will have the same
             * name as a Request, but this is not required or checked.
             *
             * (I'd prefer to have an alternative constructor, but without being able
             * to raise exceptions, there is no good way of reporting that inReplyTo is
             * not a Request that we should be making a Reply for.)
             *
             * Returns 0 on success, or -EBADMSG if inReplyTo was not a Request that
             * wanted us to reply (i.e., it did not have the WantYouToReply flag set).
             */
            int BecomesReplyTo(const Message& inReplyTo);

            /*
             * Makes this message a Stateful Request.
             *
             * A stateful request is a request that indicates the intended
             * recipient (and thus has its 'to' field set). KBUS will then cause
             * the Send of the message to fail if the Ksock bound to receive the
             * request does not have the indicate Ksock id.
             *
             * `earlier_msg` is either a Reply message from the desired Ksock, or a
             * previous Stateful Request to the same Ksock.
             *
             * If earlierMessage is a Reply, then the 'to' and 'final_to' fields for
             * the new message will be set to the 'from' and 'orig_from' fields in the old.
             *
             * If earlierMessage is a Stateful Request, then the 'to' and 'final_to'
             * fields for the new message will be copied from the old.
             *
             * In either of those cases, the message flags will have the WantReply
             * bit set.
             *
             * If earlierMessage is neither a Reply nor a Stateful Request, then
             * -EBADMSG will be returned.
             *
             * (This assumes that a stateful dialogue will normally have had a
             * message exchange to set the dialogue up.)
             *
             * (I'd prefer to have an alternative constructor, but without being able
             * to raise exceptions, there is no good way of reporting that
             * earlierMessage is not a Reply or Stateful Request.)
             *
             * Returns 0 on success, or -EBADMSG if earlierMessage is neither a
             * Reply nor a previous StatefulRequest.
             */
            int BecomesStatefulRequest(const Message& earlierMessage);

            /*
             * Return a pointer to this message's data
             *
             * This will be NULL if there is no data, or if the data length is 0.
             */
            const uint8_t *GetData() const;

            /*
             * Return the length of this message's data.
             */
            size_t GetDataLength() const;

            /*
             * Return the message's name.
             *
             * If the message is empty, this will be "", the zero length string.
             */
            const std::string& GetName()  const { return mName; }

            /*
             * Is this an "entire" or "pointy" message? Should we care?
             */
            bool IsEntire() const { return mIsEntire; }

            /*
             * Is this message "empty"?
             *
             * An empty message is one that has been constructed with the empty
             * constructory ('Message()'), and has not yet been written to by the
             * KSock 'Receive()' method.
             */
            bool IsEmpty() const { return mIsEmpty; }

            /*
             * Return the message's flags.
             */
            uint32_t GetFlags() const;

            /*
             * Set the message's flags.
             *
             * Returns 0 if it works, -1 if the message is empty.
             */
            int SetFlags(const uint32_t newFlags);

            /*
             * Returns true if 'WantReply' flag is set. This is the definition of a
             * Request.
             */
            bool IsRequest() const;

            /*
             * Returns true if the 'WantReply' and 'WantYouToReply' flags are both set.
             *
             * If it returns true, then this is a Request to which we should reply.
             */
            bool WantsUsToReply() const;

            /*
             * Checks if the 'WantReply' flag is set, and the 'to' Ksock id is also
             * set (i.e., not 0). This is the definition of a Stateful Request.
             */
            bool IsStatefulRequest() const;

            /*
             * Checks if the 'in_reply_to' field is set (i.e., not {0,0}. This is the
             * definition of a Reply.
             */
            bool IsReply() const;

            /*
             * Is this a Replier Bind Event?
             */
            bool IsReplierBindEvent() const;

            /*
             * Return the (data) parts of a Replier Bind Event.
             *
             * Returns 0 for success, or -1 if this is not actually a Replier Bind
             * Event, in which case the parameters will be unaltered.
             */
            int GetReplierBindEventData(bool& isBind, uint32_t& binder,
                    std::string& messageName) const;

            /*
             * Get the message's message id.
             *
             * Returns 0 for success, or -1 if this is an empty message, in which case
             * the parameters will be unaltered.
             */
            int GetMessageId(MessageId& outMessageId) const;

            /*
             * Get the id of the message that this is a reply to (if any).
             *
             * Note that a message which is not a reply will have an "unset"
             * 'inReplyTo' field (i.e., a message id of {0,0}.
             *
             * Returns 0 for success, or -1 if this is an empty message, in which case
             * the parameters will be unaltered.
             */
            int GetInReplyTo(MessageId& outMessageId) const;

            /*
             * Get the message's 'to' id, i.e., the Ksock id of the message recipient.
             *
             * When a message has been 'Receive()'ed, its 'to' field will be set to the
             * Ksock id of the Ksock used to read it.
             *
             * When a "normal" message is created, its 'to' field is 0. A stateful
             * request has its 'to' field set to the Ksock id of the intended recipient
             * - this is what makes it a stateful request.
             *
             * Returns 0 for success, or -1 if this is an empty message, in which case
             * the parameters will be unaltered.
             */
            int GetTo(uint32_t& outKsockId) const;

            /*
             * Get the message's 'from' id, i.e., the Ksock id of the message sender.
             *
             * Returns 0 for success, or -1 if this is an empty message, in which case
             * the parameters will be unaltered.
             */
            int GetFrom(uint32_t& outKsockId) const;

            /*
             * Get the message's 'originally from' field.
             *
             * The 'originally from' field is used by Limpets to indicate the original
             * source of a message. KBUS itself does not touch its contents. In the
             * absence of Limpets, this will normally be set to {0,0}.
             *
             * Returns 0 for success, or -1 if this is an empty message, in which case
             * the parameters will be unaltered.
             */
            int GetOrigFrom(OrigFrom& outOrigFrom) const;

            /*
             * Get the message's 'finally to' field.
             *
             * The 'finally to' field is used by Limpets to indicate the final
             * destination of a message. KBUS itself does not touch its contents. In
             * the absence of Limpets, this will normally be set to {0,0}.
             *
             * Returns 0 for success, or -1 if this is an empty message, in which case
             * the parameters will be unaltered.
             */
            int GetFinalTo(OrigFrom& outFinalTo) const;

            std::string ToString() const;

        protected:
            //! Popular class, Ksock :-)
            friend class Ksock;

            void SetData(const uint8_t *inData, const uint32_t inDataLen,
                    const uint32_t msgFlags);

            // Is this message "empty"?
            // We could just test the length of mName, which will be zero if the
            // message is empty, but it's presumably slightly quicker to have a
            // boolean to check directly, even if it does mean we have to maintain
            // it...
            bool mIsEmpty;

            //! Is this an "entire" message (as opposed to "pointy")
            bool mIsEntire;

            // The message name. We have our own copy of this.
            std::string mName;

            // The recommended way to store binary data in STL.
            // (http://stackoverflow.com/questions/441203/proper-way-to-store-binary-data-with-c-stl)
            //
            // For "pointy" messages, this will contain the message header.
            //
            // For "entire" messages, this will contain the message header and then
            // the rest of the message (name and data), with appropriate padding,
            // sentinels, etc.
            std::vector<uint8_t> mData;

            // For "pointy" messages, we use these to remember the location and
            // size of the message data (if any).
            const uint8_t *mPointyData;
            uint32_t mPointyLen;
    };


    /** This class represents a KBUS device.
     *
     * Some KBUS operations occur at a device level - i.e., they affect
     * all use of the device. This class presents that level of interface.
     *
     * It is not necessary to create a Device instance in order to instantiate
     * a Ksock, as the KBUS device number may also be used.
     *
     * The Device class will open a KBUS device as necessary. It may also
     * close it in between method calls, but this is not guaranteed.
     *
     * There is no attempt to stop a user from instantiating more than one
     * Device instance for the same KBUS device (although doing so may prove
     * confusing). Two Device instances with the same device number are
     * considered equal (regardless of their name).
     *
     * When creating a new Device object, the caller may specify the required
     * KBUS device number, or the name of the device. In the former case, the
     * new object will know both the device number and the equivalent device
     * name, but in the latter case it will only know the name. This is because
     * it is assumed that the latter may be used in cases where the system
     * names devices in a non-standard manner, and thus the dvice may not
     * follow the pattern '/dev/kbus<n>'.
     */
    class Device
    {
        public:

            // NB: Only the ios::in and ios::out modes are "listened" to
            Device(const Device& other) :
                mDeviceNumber(other.mDeviceNumber),
                mDeviceName(other.mDeviceName),
                mDeviceMode(std::ios::in | std::ios::out),
                mFd(-1) { }

            Device(const unsigned inDeviceNumber,
                   std::ios::openmode inMode=(std::ios::in | std::ios::out));

            Device(const std::string& inDeviceName,
                   std::ios::openmode inMode=(std::ios::in | std::ios::out)) :
                mDeviceNumber(-1),
                mDeviceName(inDeviceName),
                mDeviceMode(inMode),
                mFd(-1) { }

            ~Device();

            Device& operator=(const Device& other);

            /**
             * Are we open?
             *
             * Note that this may be transient.
             */
            bool IsOpen() const { return (mFd >= 0); }

            /* Tell KBUS to output verbose messages to the system log.
             * The default is to be relatively quiet.
             *
             * Note that although this is done via a KBUS device, the setting
             * actually applies to the entire KBUS kernel module, and all KBUS
             * devices.
             *
             * Returns 0 for success, and otherwise -errno.
             */
            int MakeKernelVerbose(const bool inVerbose);

            /* Find out if KBUS is outputting verbose messages to the system log.
             *
             * Note that although this is done via a KBUS device, the setting
             * actually applies to the entire KBUS kernel module, and all KBUS
             * devices.
             *
             * Returns 0 for success, and otherwise -errno.
             */
            int QueryKernelVerbose(bool& outVerbose) const;

            /*
             * Tell the KBUS device to report Replier Bind events.
             *
             * This is used by Limpets to provide the information they need
             * to proxy messages across Ksock boundaries.
             *
             * Returns 0 for success, and otherwise -errno.
             */
            int ReportReplierBinds(const bool shouldReport);

            /*
             * Find out whether we are reporting Replier Bind events
             *
             * Returns 0 for success, and otherwise -errno.
             */
            int ReportingReplierBinds(bool& areReporting) const;

            /*
             * Add another KBUS device.
             *
             * This adds another KBUS device to the already extant devices. The new
             * device will have a device number greater than any existing KBUS
             * devices. The new device number is returned as outNumber.
             *
             * Returns 0 for success, and otherwise -errno.
             */
            int AddNewDevice(unsigned& outNumber);

            /*
             * Retrieve the device number for this Device instance.
             *
             * This will be -1 if none was set.
             */
            int GetDeviceNumber() const { return mDeviceNumber; }

            /*
             * Retrieve the device name for this Device instance.
             */
            std::string GetDeviceName() const { return mDeviceName; }

            /*
             * Retrieve the opening mode for this device.
             *
             * Note that only the ios::in and ios::out flags are relevant,
             * any other flags are ignored.
             */
            std::ios::openmode GetDeviceMode () const { return mDeviceMode; }

            /*
             * Find the Ksock id of the Replier bound to the given message
             *
             * Returns 1 if there was one, 0 if there wasn't (in which case
             * outKsockId will also be 0), -errno on error.
             */
            int FindReplier(uint32_t &outKsockId, const std::string& inMessageName);

            const std::string ToString(bool inner=false) const;

        protected:
            friend class Ksock;

            int EnsureOpen() const;
            void MaybeClose() const;
            int Close() const;

            //! Number of this device, -1 if it doesn't have one
            int mDeviceNumber;

            //! Name of this device, empty if it doesn't have one.
            std::string mDeviceName;

            //! What mode to open this device with (and thus which mode
            // it *has* been opened with). Only the ios::in and ios::out
            // flags are relevant, any other flags will be ignored.
            //
            // TODO: It would be nice if the mode as stored only ever had the
            // flags we care about set on it...
            std::ios::openmode mDeviceMode;

            //! The underlying Ksock file descriptor (or, of course, -1)
            // (it's mutable because we might well have a 'const' Device, but
            // underneath/inside we may well want to repeatedly open/close the
            // actual file descriptor.
            mutable int mFd;
    };

    /** Represents a ksock */
    class Ksock
    {
        public:
            Ksock() : mDevice(Device(0)) { }

            Ksock(const Device& inDevice) :
                mDevice(inDevice) { }

            // NB: Only the ios::in and ios::out modes are "listened" to
            Ksock(const unsigned inDeviceNumber,
                 std::ios::openmode inMode=(std::ios::in | std::ios::out)) :
                mDevice(inDeviceNumber, inMode) { }

            Ksock(const std::string& inDeviceName,
                  std::ios::openmode inMode=(std::ios::in | std::ios::out)) :
                mDevice(inDeviceName, inMode) { }

            ~Ksock();

            const Device& GetDevice() const { return mDevice; }

            /**
             * Without exceptions, we need Open() so that we can report an error
             * opening the KBUS device. Similarly for Close() and any destructor
             * (although explicit close may be a good thing anyway).
             *
             * The alternative would be lazy opening the first time we try to do
             * anything with a Ksock, which imposes a small overhead on every call.
             *
             *  @return 0 on success, -errno otherwise.
             */
            int Open();

            /*
             *  @return 0 on success, -errno otherwise.
             */
            int Close();

            /** Are we open? */
            bool IsOpen() const { return mDevice.IsOpen(); }

            /** Bind
             *
             * @return 0 on success, -errno otherwise.
             */
            int Bind(const std::string& inName, bool asReplier=false);

            /** Unbind
             *
             * @return 0 on success, -errno otherwise.
             */
            int Unbind(const std::string& inName, bool asReplier=false);

            /** Retrieve the id for this ksock into outId
             *
             * @return 0 on success, -1 on failure
             */
            int GetId(uint32_t &outId) const;

            /** Retrieve the last mesasage id written on this Ksock
             *
             * @return 0 on success, -1 on failure.
             */
            int GetLastMessageId(MessageId& outMessageId) const;

            /** Set the maximum number of unread mesages that can be queued for
             *  this Ksock. Trying to set the maximum number to 0 will have no
             *  effect.
             */
            int SetMaxUnreadMessages(const uint32_t qlen);

            /** Get the maximum number of unread messages that can be queued for
             *  this Ksock
             */
            int GetMaxUnreadMessages(uint32_t& qlen) const;

            /**
             * Get the number of unread messages in this Ksock's read queue.
             */
            int HowManyMessagesUnread(int& outNum) const;

            /**
             * Get the number of messages marked for us to reply that we have
             * not yet replied to.
             */
            int HowManyMessagesUnrepliedTo(int& outNum) const;

            /** When we are multiply bound, receive messages only once?
             *
             * @param[in] shouldReceiveOnce  if true, we will receive bound messages
             *                                only once. Otherwise, we will receive them
             *                                multiple times.
             * @return 0 on success, < 0 on error.
             */
            int ReceiveMessagesOnlyOnce(const bool shouldReceiveOnce);

            /** Will messages be received only once? */
            int WillReceiveOnlyOnce(bool& onlyOnce) const;

            /** Send a message
             */
            int Send(Message& ioMessage, MessageId *msgId=NULL);

            /** Send a request message
             *
             * Marks the ioMessage as a request before it sends it.
             */
            int SendRequest(Message& ioMessage, MessageId *msgId=NULL);

            /** Send a reply to an earlier message.
             *
             * Marks the ioMessage as a reply (to inReplyTo) before it sends it.
             */
            int SendReply(Message& ioMessage, const Message& inReplyTo, MessageId *msgId=NULL);

            /** Send a stateful request
             *
             * Marks the ioMessage as a stateful request (using earlierMessage)
             * before it sends it.
             *
             * earlierMessage must be either a Reply message (from the desired
             * Ksock), or a previous stateful request (to the desired Ksock).
             */
            int SendStatefulRequest(Message& ioMessage, const Message& earlierMessage,
                    MessageId *msgId=NULL);

            /** Receive a message (assuming that there is one waiting)
             *
             *  Received messages are (for obvious reasons) never pointy.
             *
             *  ioMessage *must* be empty before it is passed to this method.
             *
             * @return 1 if we got one, 0 if we didn't, -errno on error.
             */
            int Receive(Message& ioMessage);

            /* @param[out] outPollFlags   On exit, tells you which set of poll flags apply to this socket.
             * @param[in] inPollFlags     Which poll flags to query.
             * @param[in] timeout         Timeout in ms. 0 -> just poll, < 0 -> infinite.
             *
             * @return 0 on success, -errno on failure.
             */
            int WaitForMessage(unsigned &outPollFlags, const unsigned inPollFlags, const int timeout);

            /** Return an fd you can poll */
            int GetFd(int& ioFd) { ioFd = mDevice.mFd; return 0; }

            const std::string ToString() const;

        private:
            // Our own copy of a representation of the underlying device.
            Device mDevice;
    };
}

std::ostream& operator<<(std::ostream& os, const cppkbus::Error::Enum inEnum);
std::ostream& operator<<(std::ostream& os, const cppkbus::OrigFrom& inOrigFrom);
std::ostream& operator<<(std::ostream& os, const cppkbus::MessageId& inMsgId);
std::ostream& operator<<(std::ostream& os, const cppkbus::Device& inDevice);
std::ostream& operator<<(std::ostream& os, const cppkbus::Message& msg);
std::ostream& operator<<(std::ostream& os, const cppkbus::Ksock& inSock);

#endif

/* End file */

// vim: set tabstop=8 softtabstop=4 shiftwidth=4 expandtab:
