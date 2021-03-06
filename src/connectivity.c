#include <unistd.h>
#include <wiringPi.h>

#include "global.h"
#include "connectivity.h"

/**
 * Callback for delivered MQTT message
 *
 * @param context context
 * @param dt token
 */
void delivered(void* context, MQTTClient_deliveryToken dt)
{
	printf("Message with token %d delivered.\n", dt);
	deliveredtoken = dt;
}
//------------------------------------------------------------------------------

/**
 * Get status from JSON
 *
 * @param json JSON
 * @param element name of the element in the JSON
 *
 * @return 0 for disabled, 1 for enabled
 */
int getStatus(JsonNode* json, const char* element)
{
	int status = 0;
	JsonNode* member = json_find_member(json, element);
	if (NULL == member)
	{
		return status;
	}

	if (member->tag == JSON_STRING)
	{
		status = atof(member->string_);
	}
	else if (member->tag == JSON_BOOL)
	{
		status = (int) member->bool_;
	}
	else if (member->tag == JSON_NUMBER)
	{
		status = member->number_;
	}
	return (1 <= status) ? 1 : 0;
}
//------------------------------------------------------------------------------

/**
 * Is JSON valid?
 *
 * @param node JSON
 *
 * @return 0 if JSON is valid or positive value on error
 */
int isJsonValid(JsonNode* node)
{
	if (NULL == node)
	{
		printf("ERROR: Invalid JSON\n");
		return 1;
	}

	char errmsg[256];
	if (!json_check(node, errmsg))
	{
		printf("ERROR: Corrupted JSON: %s\n", errmsg);
		return 2;
	}

	return 0;
}
//------------------------------------------------------------------------------

/**
 * Callback for receiving MQTT message
 *
 * @param context context
 * @param topicName MQTT topic
 * @param topicLen lenth of the topic
 * @param message MQTT message
 *
 * @return 1
 */
int msgarrvd(void* context, char* topicName, int topicLen, MQTTClient_message* message)
{
	// Get the message
	char* payload = (char*) malloc(message->payloadlen+1);
	for(int index=0; index<message->payloadlen; index++)
	{
		payload[index] = ((char *) message->payload)[index];
	}
	payload[message->payloadlen] = 0;

	printf("Message arrived\n");
	printf("topic: %s\n", topicName);
	printf("message: %s\n", payload);

	// Parse MQTT levels
	char* level = strtok(topicName ,"//");
	char** levels = NULL;
	int counter = 0;
	while (NULL != level)
	{
		levels = realloc(levels, (counter+1)*sizeof(char*));
		levels[counter] = level;
		counter++;
		level = strtok(NULL, "//");
	}

	if ( (2 <= counter) && (0 == strcmp(levels[1], TOPICACTION)) )
	{
		// Detect action and parse JSON from the payload
		JsonNode* node = json_decode(payload);
		if (0 == isJsonValid(node))
		{
			if (0 == strcmp(levels[2], TOPICBUZZER))
			{
				status.buzzer = getStatus(node, "status");
			}
			else if (0 == strcmp(levels[2], TOPICRELAY))
			{
				status.relay = getStatus(node, "status");
				if (1 == status.relay)
				{
					digitalWrite(PINRELAY, HIGH);
				}
				else
				{
					digitalWrite(PINRELAY, LOW);
				}
			}
			else if (0 == strcmp(levels[2], TOPICRGBLED))
			{
				int red = (1 == getStatus(node, "red")) ? HIGH : LOW;
				digitalWrite(PINRGBLED3, red);
				int green = (1 == getStatus(node, "green")) ? HIGH : LOW;
				digitalWrite(PINRGBLED2, green);
				int blue = (1 == getStatus(node, "blue")) ? HIGH : LOW;
				digitalWrite(PINRGBLED1, blue);
				status.rgbLed = ( (HIGH == red) && (HIGH == green) && (HIGH == blue) ) ? 0 : 1;
				printf("LED red: %d, green: %d, blue: %d\n", red, green, blue);
			}
		}
		json_delete(node);
	}

	// Free memory
	free(levels);
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	free(payload);
	return 1;
}
//------------------------------------------------------------------------------

/**
 * Callback for lost connectivity
 *
 * @param context context
 * @param cause error message
 */
void connlost(void *context, char *cause)
{
	printf("\nERROR: Connection lost.\n");
	// Try to reconnect
	while (MQTTCLIENT_SUCCESS != mqttConnect())
	{
		printf("Trying to reconnect in 10 seconds...\n");
		sleep(10);
	}
	// Subscribe again
	mqttSubscribe();
	printf("Successfully reconnected to MQTT broker\n");
}
//------------------------------------------------------------------------------

/**
 * Allocate MQTT topic combined from machine ID and suffix
 *
 * @param suffix suffix with additional levels for the MQTT topic
 *
 * @return MQTT topic
 */
char* createMqttTopic(char* suffix)
{
	// Make space for the machine id, slash and the topic
	size_t lenMachine = strlen(machineId);
	size_t lenTopic = strlen(suffix);
	char *mqttTopic = (char*) malloc(lenMachine + 3 + lenTopic);
	// MQTT topic's 1st level is the machine ID
	memcpy(mqttTopic, machineId, lenMachine);
	// Separate levels
	memcpy(mqttTopic+lenMachine, "/", 2);
	// Add next levels in MQTT topic
	memcpy(mqttTopic+lenMachine+1, suffix, lenTopic+1);
	return mqttTopic;
}
//------------------------------------------------------------------------------

/**
 * Add machine ID as prefix to the topic and publish MQTT message
 *
 * @param topic MQTT topic
 * @param message MQTT payload
 * @param qos MQTT QoS (0, 1 or 2)
 * @param retain set to 0 to disable or 1 to enable retain flag
 *
 */
void publish(char* topic, char* message, int qos, int retain)
{
	char *mqttTopic = createMqttTopic(topic);

	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	pubmsg.payload = message;
	pubmsg.payloadlen = strlen(message);
	pubmsg.qos = qos;
	pubmsg.retained = retain;
	MQTTClient_publishMessage(client, mqttTopic, &pubmsg, &token);
	printf("Publishing message on topic %s\n", mqttTopic);

	free(mqttTopic);
}
//------------------------------------------------------------------------------

/**
 * Publish data from sensors as MQTT message with QoS 1 and enabled retain.
 *
 * @param topic MQTT topic
 * @param json MQTT payload serialized as JSON
 *
 */
void publishSensorData(char* topic, char* json)
{
	JsonNode* node = json_decode(json);
	char* messagePayload = json_stringify(node, "\t");
	publish(topic, messagePayload, 1, 1);
	free(messagePayload);
	json_delete(node);
}
//------------------------------------------------------------------------------

/**
 * Gracefully disconnect from the MQTT broker
 *
 */
void mqttDisconnect()
{
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);
}
//------------------------------------------------------------------------------

/**
 * Connect the MQTT broker
 *
 * @return MQTTCLIENT_SUCCESS if the client successfully connects to the server; Positive value on error
 */
int mqttConnect()
{
	// Free the memory allocated to the MQTT client
	MQTTClient_destroy(&client);

	// Try to establish connection to MQTT broker
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

        MQTTClient_create(&client, config.address, config.clientId,
        MQTTCLIENT_PERSISTENCE_NONE, NULL);
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;

	// Register again callbacks
	MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

        return MQTTClient_connect(client, &conn_opts);
}
//------------------------------------------------------------------------------

/**
 * Subscribe to MQTT topics
 *
 */
void mqttSubscribe()
{
	char *mqttTopic = createMqttTopic(TOPICACTIONS);
	int status = MQTTClient_subscribe(client, mqttTopic, 2);
	if (MQTTCLIENT_SUCCESS == status)
	{
		printf("Subscribed to topic: %s\n", mqttTopic);
	}
	else
	{
		printf("ERROR: Unable to subscribe to MQTT broker %d\n", status);
	}
	free(mqttTopic);
}
//------------------------------------------------------------------------------
