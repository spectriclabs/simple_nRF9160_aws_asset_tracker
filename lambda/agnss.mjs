import * as https from 'node:https';
import { IoTDataPlaneClient, PublishCommand } from "@aws-sdk/client-iot-data-plane";
import { SSMClient, GetParameterCommand } from "@aws-sdk/client-ssm";

// Chunk size, set this small enough to allow the device to process the message
const chunk_size = 1600;
const mqtt_topic = "nrfcloud/agps";

// Connect to the IoT service to send MQTT messages
const client = new IoTDataPlaneClient({
    region: "us-east-1",
});

// Get the service key to access the nRFCloud
const ssm_client = new SSMClient({});
const ssmcommand = new GetParameterCommand({ Name: "/nrfcloud/servicekey" });
const ssmresponse = await ssm_client.send(ssmcommand);
const servicekey = ssmresponse.Parameter.Value;

/**
 * Parse content range headers
 */
function parse_content_range(input) {
  const matches = input.match(/^(\w+) ((\d+)-(\d+)|\*)\/(\d+|\*)$/);
  if (!matches) return null;
  const [, unit, , start, end, size] = matches;
  const range = {
    unit,
    start: start != null ? Number(start) : null,
    end: end != null ? Number(end) : null,
    size: size === "*" ? null : Number(size),
  };
  if (range.start === null && range.end === null && range.size === null)
    return null;
  return range;
}

/**
 * Request the AGNSS data and publish it to MQTT
 */
export const handler = (event, context, callback) => {
    console.log("AGNSS request " + JSON.stringify(event));
    
    let range = {
        start: 0,
        end: chunk_size - 1,
    }
    let options = {
        hostname: "api.nrfcloud.com",
        path: "/v1/location/agnss",
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "Authorization": "Bearer " + servicekey,
            "Accept": "application/octet-stream",
            "Range": `bytes=${range.start}-${range.end}`,
        }
    }
    
    // If no event types are requested get all
    if (!event.types) {
        event.types = [ 1,2,3,4,5,6,7,8,9,10,11,12,13 ]
    }
    
    let agps_b64 = ''; // base64 encoding of the entire agps message
    
    const chunk_handler = (res) => {
        let chunks = [];
        
        console.log('Status:', res.statusCode);
        console.log('Headers:', JSON.stringify(res.headers));
        
        res.setEncoding('binary');
        res.on('data', (chunk) => { chunks.push(Buffer.from(chunk, 'binary')); });
        res.on('end', () => {
            if ((res.statusCode === 200) || (res.statusCode === 206)) {
                console.log('Successfully processed HTTPS response');
                
                // Concat all the chunks
                const agps = Buffer.concat(chunks);
                const content_length = Number(res.headers["content-length"])
                const content_range = parse_content_range(res.headers["content-range"])
                
                // Concat the b64 values
                agps_b64 = agps_b64 + Buffer.from(agps).toString('base64');
                
                // Send the MQTT message
                const input = {
                    topic: mqtt_topic,
                    qos: 0,
                    retain: false,
                    payload: agps,
                    payloadFormatIndicator: "UNSPECIFIED_BYTES",
                    contentType: "application/octet-stream"
                };
                const command = new PublishCommand(input);
                console.log(`Sending MQTT publish ${agps.length} bytes`);
                client.send(command).then(console.log);
                
                // Determine how many bytes remain
                let bytes_remain = 0;
                if (content_range) {
                    bytes_remain = content_range.size - content_range.end - 1;
                }
                
                // If bytes remain make a request for the next chunk, otherwise
                // finish the lambda expression
                console.log(`AGNSS bytes remain: ${bytes_remain}`)
                if (bytes_remain > 0) {
                    range.start = range.end + 1;
                    range.end = range.start + chunk_size - 1;
                    
                    options.headers["Range"] = `bytes=${range.start}-${range.end}`
                    
                    console.log("Request Headers: " + JSON.stringify(options));
                    
                    const req = https.request(options, chunk_handler);
                    req.on('error', callback);
                    req.write(JSON.stringify(event));
                    req.end();
                } else {
                    // Upon completion publish the entire b64 encoded APGS
                    callback(null, {
                        "agps": agps_b64
                    });
                }
            } else {
                // Failure to process, the chunks likely contains an error message
                console.log('Failed to process nRF cloud response');
                const err = Buffer.concat(chunks).toString("utf8");
                console.log(err);
                callback(null, {
                    "err": err
                });
            }
        });
    };
    
    // Make the initial request
    console.log("Request Headers: " + JSON.stringify(options));
    const req = https.request(options, chunk_handler);
    req.on('error', callback);
    req.write(JSON.stringify(event));
    req.end();
};