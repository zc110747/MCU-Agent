// SnmpCore.cs — Minimal SNMP v2c BER codec + message builder/parser.
// SHARED by snmp_client and snmp_server via Compile Include (kept in one place).
//
// Namespace is left unqualified-friendly: both tools reference it through the
// same source file, so we declare it under a neutral namespace SnmpCommon.

using System;
using System.Collections.Generic;
using System.Text;

namespace SnmpCommon
{
    // BER tag constants (subset used by SNMP).
    public static class Ber
    {
        public const byte INTEGER = 0x02;
        public const byte OCTET_STRING = 0x04;
        public const byte NULL = 0x05;
        public const byte OID = 0x06;
        public const byte SEQUENCE = 0x30;

        public const byte APP_IPADDRESS = 0x40;
        public const byte APP_COUNTER32 = 0x41;
        public const byte APP_GAUGE32 = 0x42;
        public const byte APP_TIMETICKS = 0x43;

        public const byte CTX_GetRequest = 0xA0;
        public const byte CTX_GetNextRequest = 0xA1;
        public const byte CTX_GetResponse = 0xA2;
        public const byte CTX_SetRequest = 0xA3;
        public const byte CTX_Trap = 0xA4;

        public const int VERSION_v2c = 1;
    }

    // A single value carried in a VarBind.
    public class SnmpValue
    {
        public byte Tag;          // BER tag (UNIVERSAL or APPLICATION)
        public int IntValue;      // for INTEGER
        public uint UIntValue;    // for Counter/Gauge/TimeTicks
        public byte[] Bytes = Array.Empty<byte>(); // OCTET STRING / IP / OID raw

        public string AsString()
        {
            if (Tag == Ber.OCTET_STRING) return Encoding.ASCII.GetString(Bytes);
            if (Tag == Ber.APP_IPADDRESS) return $"{Bytes[0]}.{Bytes[1]}.{Bytes[2]}.{Bytes[3]}";
            if (Tag == Ber.INTEGER) return IntValue.ToString();
            if (Tag == Ber.APP_COUNTER32 || Tag == Ber.APP_GAUGE32 || Tag == Ber.APP_TIMETICKS)
                return UIntValue.ToString();
            if (Tag == Ber.OID) return SnmpCodec.OidToString(Bytes);
            if (Tag == Ber.NULL) return "";
            return BitConverter.ToString(Bytes);
        }
    }

    // A VarBind = OID + value.
    public class VarBind
    {
        public uint[] Oid = Array.Empty<uint>();
        public SnmpValue Value = new SnmpValue { Tag = Ber.NULL };
    }

    public static class SnmpCodec
    {
        // ---------- OID helpers ----------
        public static byte[] OidToBytes(uint[] arcs)
        {
            var outp = new List<byte>();
            if (arcs.Length < 2) throw new ArgumentException("OID needs >=2 arcs");
            outp.Add((byte)(40 * arcs[0] + arcs[1]));
            for (int i = 2; i < arcs.Length; i++)
            {
                uint v = arcs[i];
                if (v == 0) { outp.Add(0); continue; }
                var tmp = new List<byte>();
                while (v > 0) { tmp.Add((byte)(v & 0x7F)); v >>= 7; }
                tmp.Reverse();
                for (int j = 0; j < tmp.Count; j++)
                    outp.Add((byte)(tmp[j] | (j < tmp.Count - 1 ? 0x80 : 0)));
            }
            return outp.ToArray();
        }

        public static uint[] OidFromBytes(byte[] raw)
        {
            var arcs = new List<uint>();
            int first = raw[0];
            int a0 = first / 40; if (a0 > 2) a0 = 2;
            arcs.Add((uint)a0);
            arcs.Add((uint)(first - a0 * 40));
            int i = 1;
            while (i < raw.Length)
            {
                uint v = 0;
                while (i < raw.Length)
                {
                    byte b = raw[i++];
                    v = (v << 7) | (uint)(b & 0x7F);
                    if ((b & 0x80) == 0) break;
                }
                arcs.Add(v);
            }
            return arcs.ToArray();
        }

        public static string OidToString(byte[] raw) => string.Join(".", OidFromBytes(raw));
        public static string OidToString(uint[] arcs) => string.Join(".", arcs);

        // ---------- low-level TLV ----------
        public static void WriteLen(List<byte> buf, int len)
        {
            if (len < 0x80) { buf.Add((byte)len); return; }
            var tmp = new List<byte>();
            int v = len;
            while (v > 0) { tmp.Add((byte)(v & 0xFF)); v >>= 8; }
            buf.Add((byte)(0x80 | tmp.Count));
            tmp.Reverse();
            buf.AddRange(tmp);
        }

        public static int ReadLen(byte[] buf, ref int p)
        {
            int b = buf[p++];
            if (b < 0x80) return b;
            int n = b & 0x7F;
            int v = 0;
            for (int i = 0; i < n; i++) v = (v << 8) | buf[p++];
            return v;
        }

        // ---------- value encoders ----------
        public static void EncodeInteger(List<byte> parent, int v)
        {
            var body = new List<byte>();
            uint uv = (uint)v;
            byte[] b4 = { (byte)(uv >> 24), (byte)(uv >> 16), (byte)(uv >> 8), (byte)uv };
            int start = 0;
            while (start < 3 && ((b4[start] == 0x00 && (b4[start + 1] & 0x80) == 0) ||
                                 (b4[start] == 0xFF && (b4[start + 1] & 0x80) != 0)))
                start++;
            for (int i = start; i < 4; i++) body.Add(b4[i]);
            Wrap(parent, Ber.INTEGER, body);
        }

        public static void EncodeU32App(List<byte> parent, byte apptag, uint v)
        {
            var body = new List<byte>();
            byte[] b4 = { (byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v };
            int start = 0;
            while (start < 3 && b4[start] == 0x00) start++;
            for (int i = start; i < 4; i++) body.Add(b4[i]);
            Wrap(parent, (byte)(0x40 | apptag), body);
        }

        public static void EncodeOctet(List<byte> parent, byte[] s) => Wrap(parent, Ber.OCTET_STRING, new List<byte>(s));
        public static void EncodeNull(List<byte> parent) => Wrap(parent, Ber.NULL, new List<byte>());
        public static void EncodeOid(List<byte> parent, uint[] arcs) => Wrap(parent, Ber.OID, new List<byte>(OidToBytes(arcs)));

        public static void Wrap(List<byte> parent, byte tag, List<byte> body)
        {
            parent.Add(tag);
            WriteLen(parent, body.Count);
            parent.AddRange(body);
        }

        // ---------- message builders ----------
        // Helper: assemble a full SNMP message = SEQUENCE { version, community, pdu }
        // The pdu (GetRequest/GetNext/SetRequest/GetResponse) is passed in.
        static byte[] BuildMessage(byte pduType, int reqId, string community, List<byte> pdu)
        {
            // PDU := pduType SEQUENCE { reqId, errStatus, errIndex, varBindList }
            var pduFull = new List<byte>();
            EncodeInteger(pduFull, reqId);
            EncodeInteger(pduFull, 0);
            EncodeInteger(pduFull, 0);
            pduFull.AddRange(pdu);
            var pduWrapped = new List<byte>();
            Wrap(pduWrapped, pduType, pduFull);

            // message content in order: version, community, pdu
            var inner = new List<byte>();
            EncodeInteger(inner, Ber.VERSION_v2c);
            EncodeOctet(inner, Encoding.ASCII.GetBytes(community));
            inner.AddRange(pduWrapped);

            var msg = new List<byte>();
            Wrap(msg, Ber.SEQUENCE, inner);
            return msg.ToArray();
        }

        public static byte[] BuildRequest(byte pduType, int reqId, string community,
                                          List<uint[]> oids)
        {
            var vbl = new List<byte>();
            foreach (var oid in oids)
            {
                var vb = new List<byte>();
                EncodeOid(vb, oid);
                EncodeNull(vb);
                Wrap(vbl, Ber.SEQUENCE, vb);
            }
            var pdu = new List<byte>();
            Wrap(pdu, Ber.SEQUENCE, vbl);
            return BuildMessage(pduType, reqId, community, pdu);
        }

        public static byte[] BuildSet(byte pduType, int reqId, string community,
                                      uint[] oid, SnmpValue val)
        {
            var vb = new List<byte>();
            EncodeOid(vb, oid);
            if (val.Tag == Ber.INTEGER) EncodeInteger(vb, val.IntValue);
            else if (val.Tag == Ber.OCTET_STRING) EncodeOctet(vb, val.Bytes);
            else EncodeNull(vb);
            var vbl = new List<byte>();
            Wrap(vbl, Ber.SEQUENCE, vb);
            var pdu = new List<byte>();
            Wrap(pdu, Ber.SEQUENCE, vbl);
            return BuildMessage(pduType, reqId, community, pdu);
        }

        // Build a GetResponse (used by the server tool when simulating an agent).
        public static byte[] BuildResponse(byte pduType, int reqId, string community,
                                            List<uint[]> oids, List<SnmpValue> vals)
        {
            var vbl = new List<byte>();
            for (int i = 0; i < oids.Count; i++)
            {
                var vb = new List<byte>();
                EncodeOid(vb, oids[i]);
                var v = vals[i];
                if (v.Tag == Ber.INTEGER) EncodeInteger(vb, v.IntValue);
                else if (v.Tag == Ber.APP_COUNTER32 || v.Tag == Ber.APP_GAUGE32 || v.Tag == Ber.APP_TIMETICKS)
                    EncodeU32App(vb, (byte)(v.Tag & 0x0F), v.UIntValue);
                else if (v.Tag == Ber.APP_IPADDRESS) Wrap(vb, Ber.APP_IPADDRESS, new List<byte>(v.Bytes));
                else if (v.Tag == Ber.OCTET_STRING) EncodeOctet(vb, v.Bytes);
                else EncodeNull(vb);
                Wrap(vbl, Ber.SEQUENCE, vb);
            }
            var pdu = new List<byte>();
            Wrap(pdu, Ber.SEQUENCE, vbl);
            return BuildMessage(pduType, reqId, community, pdu);
        }

        // ---------- message parser (request or response) ----------
        public static bool ParseMessage(byte[] pkt, out byte pduType, out int reqId,
                                        out int errStatus, out int errIndex,
                                        out List<VarBind> vars, out string community)
        {
            pduType = 0; reqId = 0; errStatus = 0; errIndex = 0;
            vars = new List<VarBind>(); community = "";
            try
            {
                int p = 0;
                if (pkt[p++] != Ber.SEQUENCE) return false;
                ReadLen(pkt, ref p);
                if (pkt[p++] != Ber.INTEGER) return false;
                int vl = ReadLen(pkt, ref p); p += vl;           // version
                if (pkt[p++] != Ber.OCTET_STRING) return false;
                int cl = ReadLen(pkt, ref p);
                community = Encoding.ASCII.GetString(pkt, p, cl); p += cl;
                pduType = pkt[p++];
                ReadLen(pkt, ref p);
                if (pkt[p++] != Ber.INTEGER) return false;
                vl = ReadLen(pkt, ref p); reqId = 0; for (int i = 0; i < vl; i++) reqId = (reqId << 8) | pkt[p++];
                if (pkt[p++] != Ber.INTEGER) return false;
                vl = ReadLen(pkt, ref p); errStatus = 0; for (int i = 0; i < vl; i++) errStatus = (errStatus << 8) | pkt[p++];
                if (pkt[p++] != Ber.INTEGER) return false;
                vl = ReadLen(pkt, ref p); errIndex = 0; for (int i = 0; i < vl; i++) errIndex = (errIndex << 8) | pkt[p++];
                if (pkt[p++] != Ber.SEQUENCE) return false;
                int vblen = ReadLen(pkt, ref p);
                int vbend = p + vblen;
                while (p < vbend)
                {
                    if (pkt[p++] != Ber.SEQUENCE) break;
                    int vblen2 = ReadLen(pkt, ref p);
                    int vbend2 = p + vblen2;
                    var vb = new VarBind();
                    if (pkt[p++] != Ber.OID) break;
                    int ol = ReadLen(pkt, ref p);
                    byte[] oraw = new byte[ol]; Array.Copy(pkt, p, oraw, 0, ol); p += ol;
                    vb.Oid = OidFromBytes(oraw);
                    byte tag = pkt[p++];
                    int vl2 = ReadLen(pkt, ref p);
                    var v = new SnmpValue { Tag = tag };
                    if (tag == Ber.INTEGER)
                    {
                        int iv = 0; for (int i = 0; i < vl2; i++) iv = (iv << 8) | pkt[p++];
                        if ((pkt[p - vl2] & 0x80) != 0 && vl2 < 4) iv |= ~((1 << (vl2 * 8)) - 1);
                        v.IntValue = iv;
                    }
                    else if (tag == Ber.APP_COUNTER32 || tag == Ber.APP_GAUGE32 || tag == Ber.APP_TIMETICKS)
                    {
                        uint uv = 0; for (int i = 0; i < vl2; i++) uv = (uv << 8) | pkt[p++];
                        v.UIntValue = uv;
                    }
                    else if (tag == Ber.OCTET_STRING || tag == Ber.APP_IPADDRESS)
                    {
                        byte[] b = new byte[vl2]; Array.Copy(pkt, p, b, 0, vl2); p += vl2;
                        v.Bytes = b;
                    }
                    else { p += vl2; }
                    vb.Value = v;
                    vars.Add(vb);
                    p = vbend2;
                }
                return true;
            }
            catch { return false; }
        }

        public static string TagName(byte t) => t switch
        {
            Ber.INTEGER => "INTEGER",
            Ber.OCTET_STRING => "OCTET STR",
            Ber.OID => "OID",
            Ber.NULL => "NULL",
            Ber.APP_IPADDRESS => "IPADDR",
            Ber.APP_COUNTER32 => "Counter32",
            Ber.APP_GAUGE32 => "Gauge32",
            Ber.APP_TIMETICKS => "TimeTicks",
            _ => $"0x{t:X2}"
        };
    }
}
