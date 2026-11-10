# Contact introductions and crash alerts

Messages use one GSM-7 SMS, at most 160 units. Rider names retain their supported 1–40 characters. The text is complete at the maximum name length and the longest supported coordinate pair; essential instructions and location are not silently truncated. The sender is the vest SIM number, not necessarily the rider’s personal phone.

When **Notify added contacts** is enabled, new saved contacts receive:

```text
Hello from Alex's SmartVest!
They added you as an emergency contact. Please save this number as SmartVest for future alerts.
```

Adding a contact with notifications off sends nothing. The administrator can resend the introduction to a selected saved contact using the existing confirmed SMS action.

A qualifying automatic incident sends:

```text
SmartVest CRASH ALERT
Rider: Alex
Call their phone now. No answer? Call 911.
Location:
14.599500,120.984200
```

The alert identifies the rider, asks the contact to call their personal phone and gives a clear emergency escalation step if there is no answer. The sender is the vest SIM, so the contact should use the rider's usual personal number. The text reports a device crash alert without claiming an injury diagnosis or an automatic call to emergency services. The Philippine government lists **911** as the [National Emergency Hotline](https://ehotlines.e.gov.ph/).

A usable fix is sent as plain latitude and longitude coordinates, which the recipient can copy into a maps application. A current fix is labelled **Location:**; an older usable fix is labelled **Last known location:**. Fixes older than the existing five-minute limit remain omitted. With no usable fix, the alert ends with **Location unavailable.** It does not ask an injured rider to provide a location. The contact and emergency action remain present in every real alert. Location is frozen when Main accepts the incident and is not a reconstructed crash site. Missing GPS or cameras do not suppress SMS.

All supported 40-character names and signed coordinate extremes fit the existing 160-unit limit, including the complete coordinates and older-location label. Short test messages remain clearly labelled with no emergency action. No URL is included: [Globe blocks person to person SMS containing links](https://www.globe.com.ph/help/why-globe-blocks-texts-with-clickable-links). Plain text URLs are still URLs and do not avoid this filter. Network availability, service restrictions and recipient delivery still require device testing.

The separate all-contact diagnostic retains **SmartVest test / This is a test. No action needed.** The selected-contact **Send crash SMS** intentionally sends the normal crash-alert text after the existing confirmation; tell the recipient before using it. It creates no incident or camera lock. Carrier charges can apply. No real messages are sent by software verification or the localhost preview.

Queue priority, delivery-report matching and ambiguous-send handling retain their existing rules. The dashboard can now durably cancel one pending text or all pending texts before mobile-network handoff; see [incident records](INCIDENT_RECORDS.md). Existing queued jobs and stored incident messages retain their saved wording; only newly composed messages use these templates. Carrier acceptance does not prove recipient delivery.
