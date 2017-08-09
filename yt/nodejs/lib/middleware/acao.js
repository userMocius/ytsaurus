var utils = require("../utils");

////////////////////////////////////////////////////////////////////////////////

exports.that = function Middleware__YtAcao()
{
    return function(req, rsp, next) {
        var origin = req.headers["origin"] || "*";

        if (req.method === "GET" || req.method === "POST" || req.method === "PUT") {
            rsp.setHeader("Access-Control-Allow-Credentials", "true");
            rsp.setHeader("Access-Control-Allow-Origin", origin);
        }

        if (req.method === "OPTIONS") {
            rsp.setHeader("Access-Control-Allow-Credentials", "true");
            rsp.setHeader("Access-Control-Allow-Origin", origin);
            rsp.setHeader("Access-Control-Allow-Methods", "POST, PUT, GET, OPTIONS");
            var cors_headers = [
                "authorization",
                "origin"
                "content-type",
                "accept",
                "x-yt-parameters",
                "x-yt-parameters0",
                "x-yt-parameters-0",
                "x-yt-parameters1",
                "x-yt-parameters-1",
                "x-yt-input-format",
                "x-yt-input-format0",
                "x-yt-input-format-0",
                "x-yt-output-format",
                "x-yt-output-format0",
                "x-yt-output-format-0",
                "x-yt-header-format",
                "x-yt-suppress-redirect",
            ];
            rsp.setHeader("Access-Control-Allow-Headers", cors_headers.join(","));
            rsp.setHeader("Access-Control-Max-Age", "3600");
            return void utils.dispatchAs(rsp);
        }

        next();
    };
};
