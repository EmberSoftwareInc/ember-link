# Development channel candidate qualification — 0.3.7-dev.1

Source commit: `79774d826ad2dcf94d6cabf58abc23aeb82220e4` on `dev`.
Board: LilyGO T-Dongle-S3. Layout: `link-v1`.
Image size: 1,314,816 bytes.
SHA-256: `d423a869e71f1b5e914038bcf2d5b1d29b6db315205f8e2ff00ffb7df4a5feb3`.
The established production verification key is unchanged.

This candidate differs from stable 0.3.6 only in the two firmware version
declarations. The signed package was built from that clean committed source,
with provenance and checksums. Its RSA signature was independently verified
again during local catalog import and GitHub draft preparation.

## Physical cloud round trip

The owner selected Development and approved the exact candidate in the local
reference example. The dongle reported an `installed` receipt matching the image
hash and version, resumed authenticated polling, and reported `pendingVerify:false`.

The owner then selected Stable, explicitly approved replacement with 0.3.6, and
installed the exact previously qualified stable image (SHA-256
`ff49a1069c6986cf80ab3015bc407b977916e457a2e720733e0d3afc25b9b0f9`).
Its installed receipt, reported version, and healthy-boot status were confirmed.
Both legs preserved screen on, LED on, 180-degree orientation, and settings
revision 6. Channel selection itself did not install firmware.

## Physical Bridge USB round trip

The rebuilt macOS Bridge test app recognized Link over USB as 0.3.6 and correctly
reported no different Stable recommendation. The owner explicitly approved
publishing the candidate as a GitHub prerelease and recommending it through the
Development feed. All six assets were checked byte-for-byte before publication;
an anonymous image download matched the qualified hash, and the public Stable
feed remained on 0.3.6.

The owner used Bridge's Development selector and guided USB installer, then
reported Bridge confirmed 0.3.7-dev.1. An independent fresh cloud report confirmed
that version, `pendingVerify:false`, and unchanged display/LED settings at revision
6. Both design files matched private hashes recorded before the Bridge test.

The owner then selected Stable in Bridge and used the explicit replacement flow.
Bridge confirmed 0.3.6. A fresh independent cloud report confirmed healthy boot,
screen on, LED on, 180-degree orientation, and revision 6. Both design files again
matched the pre-Bridge hashes. The card was safely unmounted after verification.
The cloud preference remained Stable while Bridge selected Development, verifying
that the two consumer preferences are independent.

## Final machine preview

After the Bridge return to stable 0.3.6, the owner moved Link normally to the
Brother NQ1700E and confirmed that a saved design preview opened and the machine
remained responsive. This checks the returned stable image after a power cycle;
it is not a separate preview qualification of the development image. No stitching
was initiated as part of this check.

A fresh authenticated cloud poll after this power cycle reported 0.3.6, reset
reason 1, `pendingVerify:false`, and unchanged screen/LED/orientation preferences
at revision 6. The reference backend and existing test tunnel remained available.

## Validation limits

The physical Bridge tests used USB on macOS. Bridge's local Wi-Fi path, other
operating systems, and physical power cuts during an update were not repeated in
this channel test. The final stable machine-preview result is recorded above.
Earlier stable hardware qualification remains separate evidence.

The design-file baseline was captured after the cloud round trip, so it proves
preservation through the Bridge round trip only. This is a Development-channel
candidate; no new stable firmware or Bridge app release was published.

Consumer software checks passed: 36 Bridge Rust tests, 24 Bridge UI tests, 48
example Python tests, and 29 example Node tests. The Bridge frontend and local
macOS test app built successfully. The example browser flow also completed a
simulated Development-to-Stable round trip with both approvals enforced.
