/*
 * (c) Copyright, Real-Time Innovations, 2020.  All rights reserved.
 * RTI grants Licensee a license to use, modify, compile, and create derivative
 * works of the software solely for use with RTI Connext DDS. Licensee may
 * redistribute copies of the software provided that all such copies are subject
 * to this license. The software is provided "as is", with no warranty of any
 * type, including any warranty for fitness for any purpose. RTI is under no
 * obligation to maintain or support the software. RTI shall not be liable for
 * any incidental or consequential damages arising out of the use or inability
 * to use the software.
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#include "temperature.h"
#include "temperatureSupport.h"
#include "ndds/ndds_cpp.h"
#include "application.h"

using namespace application;

static int shutdown(
        DDSDomainParticipant *participant,
        const char *shutdown_message,
        int status);

// Process data. Returns number of samples processed.
unsigned int process_data(TemperatureDataReader *Temperature_reader)
{
    TemperatureSeq data_seq;
    DDS_SampleInfoSeq info_seq;
    unsigned int samples_read = 0;

    // Take available data from DataReader's queue
    DDS_ReturnCode_t retcode = Temperature_reader->take(data_seq, info_seq);

    // Iterate over all available data
    for (int i = 0; i < data_seq.length(); ++i) {
        // Check if a sample is an instance lifecycle event
        if (!info_seq[i].valid_data) {
            std::cout << "Received instance state notification" << std::endl;
            continue;
        }
        // Print data
        TemperatureTypeSupport::print_data(&data_seq[i]);
        samples_read++;
    }
    // Data sequence was loaned from middleware for performance.
    // Return loan when application is finished with data.
    Temperature_reader->return_loan(data_seq, info_seq);
    
    return samples_read;
}

int run_example(unsigned int domain_id, unsigned int sample_count)
{
    // Connext DDS Setup
    // -----------------
    // A DomainParticipant allows an application to begin communicating in
    // a DDS domain. Typically there is one DomainParticipant per application.
    // DomainParticipant QoS is configured in USER_QOS_PROFILES.xml
    DDSDomainParticipant *participant =
            DDSTheParticipantFactory->create_participant(
                    domain_id,
                    DDS_PARTICIPANT_QOS_DEFAULT,
                    NULL /* listener */,
                    DDS_STATUS_MASK_NONE);
    if (participant == NULL) {
        shutdown(participant, "create_participant error", EXIT_FAILURE);
    }

    // A Subscriber allows an application to create one or more DataReaders
    // Subscriber QoS is configured in USER_QOS_PROFILES.xml
    DDSSubscriber *subscriber = participant->create_subscriber(
            DDS_SUBSCRIBER_QOS_DEFAULT,
            NULL /* listener */,
            DDS_STATUS_MASK_NONE);
    if (subscriber == NULL) {
        shutdown(participant, "create_subscriber error", EXIT_FAILURE);
    }

    // Register the datatype to use when creating the Topic
    const char *type_name = TemperatureTypeSupport::get_type_name();
    DDS_ReturnCode_t retcode =
            TemperatureTypeSupport::register_type(participant, type_name);
    if (retcode != DDS_RETCODE_OK) {
        shutdown(participant, "register_type error", EXIT_FAILURE);
    }

    // A Topic has a name and a datatype. Create a Topic called
    // "ChocolateTemperature" with your registered data type
    DDSTopic *topic = participant->create_topic(
            "ChocolateTemperature",
            type_name,
            DDS_TOPIC_QOS_DEFAULT,
            NULL /* listener */,
            DDS_STATUS_MASK_NONE);
    if (topic == NULL) {
        shutdown(participant, "create_topic error", EXIT_FAILURE);
    }

    // This DataReader reads data of type Temperature on Topic
    // "ChocolateTemperature". DataReader QoS is configured in
    // USER_QOS_PROFILES.xml
    DDSDataReader *reader = subscriber->create_datareader(
            topic,
            DDS_DATAREADER_QOS_DEFAULT,
            NULL,
            DDS_STATUS_MASK_NONE);
    if (reader == NULL) {
        shutdown(participant, "create_datareader error", EXIT_FAILURE);
    }

    // Get status condition: Each entity has a Status Condition, which
    // gets triggered when a status becomes true
    DDSStatusCondition *status_condition = reader->get_statuscondition();
    if (status_condition == NULL) {
        shutdown(participant, "get_statuscondition error", EXIT_FAILURE);
    }

    // Enable only the status we are interested in:
    //   DDS_DATA_AVAILABLE_STATUS
    retcode = status_condition->set_enabled_statuses(DDS_DATA_AVAILABLE_STATUS);
    if (retcode != DDS_RETCODE_OK) {
        shutdown(participant, "set_enabled_statuses error", EXIT_FAILURE);
    }

    // Create the WaitSet and attach the Status Condition to it. The WaitSet
    // will be woken when the condition is triggered.
    DDSWaitSet waitset;
    retcode = waitset.attach_condition(status_condition);
    if (retcode != DDS_RETCODE_OK) {
        shutdown(participant, "attach_condition error", EXIT_FAILURE);
    }

    // A narrow is a cast from a generic DataReader to one that is specific
    // to your type. Use the type specific DataReader to read data
    TemperatureDataReader *Temperature_reader =
            TemperatureDataReader::narrow(reader);
    if (Temperature_reader == NULL) {
        shutdown(participant, "DataReader narrow error", EXIT_FAILURE);
    }

    // Main loop. Wait for data to arrive, and process when it arrives.
    // ----------------------------------------------------------------
    unsigned int samples_read = 0;
    while (running && (samples_read < sample_count || sample_count == 0)) {
        DDSConditionSeq active_conditions_seq;

        // wait() blocks execution of the thread until one or more attached
        // Conditions become true, or until a user-specified timeout expires.
        DDS_Duration_t wait_timeout = { 4, 0 };
        retcode = waitset.wait(active_conditions_seq, wait_timeout);

        // You get a timeout if no conditions were triggered before the timeout
        if (retcode == DDS_RETCODE_TIMEOUT) {
            std::cout << "Wait timed out after 4 seconds." << std::endl;
            continue;
        } else if (retcode != DDS_RETCODE_OK) {
            std::cerr << "wait returned error: " << retcode << std::endl;
            break;
        }

        // Get the status changes to check which status condition
        // triggered the the WaitSet to wake
        DDS_StatusMask triggeredmask = Temperature_reader->get_status_changes();

        // If the status is "Data Available"
        if (triggeredmask & DDS_DATA_AVAILABLE_STATUS) {
            samples_read += process_data(Temperature_reader);
        }
    }

    // Cleanup
    // -------
    // Delete all entities (DataReader, Topic, Subscriber, DomainParticipant)
    return shutdown(participant, "shutting down", 0);
}

// Delete all entities
static int shutdown(
        DDSDomainParticipant *participant,
        const char *shutdown_message,
        int status)
{
    DDS_ReturnCode_t retcode;

    std::cout << shutdown_message << std::endl;

    if (participant != NULL) {
        // This includes everything created by this Participant, including
        // DataWriters, Topics, Publishers. (and Subscribers and DataReaders)
        retcode = participant->delete_contained_entities();
        if (retcode != DDS_RETCODE_OK) {
            std::cerr << "delete_contained_entities error" << retcode
                      << std::endl;
            status = EXIT_FAILURE;
        }

        retcode = DDSTheParticipantFactory->delete_participant(participant);
        if (retcode != DDS_RETCODE_OK) {
            std::cerr << "delete_participant error" << retcode << std::endl;
            status = EXIT_FAILURE;
        }
    }
    return status;
}

// Sets Connext verbosity to help debugging
void set_verbosity(NDDS_Config_LogVerbosity verbosity)
{
    NDDSConfigLogger::get_instance()->set_verbosity(verbosity);
}

int main(int argc, char *argv[])
{
    // Parse arguments and handle control-C
    ApplicationArguments arguments;
    parse_arguments(arguments, argc, argv);
    if (arguments.parse_result == PARSE_RETURN_EXIT) {
        return EXIT_SUCCESS;
    } else if (arguments.parse_result == PARSE_RETURN_FAILURE) {
        return EXIT_FAILURE;
    }
    setup_signal_handlers();

    // Enables different levels of debugging output
    set_verbosity(arguments.verbosity);

    int status = run_example(arguments.domain_id, arguments.sample_count);

    // Releases the memory used by the participant factory.  Optional at
    // application shutdown
    DDS_ReturnCode_t retcode = DDSDomainParticipantFactory::finalize_instance();
    if (retcode != DDS_RETCODE_OK) {
        std::cerr << "finalize_instance error" << retcode << std::endl;
        status = EXIT_FAILURE;
    }

    return status;
}
