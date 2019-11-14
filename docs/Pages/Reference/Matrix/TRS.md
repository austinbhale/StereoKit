---
layout: default
title: Matrix.TRS
description: Transform, Rotate, Scale. Creates a transform Matrix using all these components!
---
# [Matrix]({{site.url}}/Pages/Reference/Matrix.html).TRS

<div class='signature' markdown='1'>
static [Matrix]({{site.url}}/Pages/Reference/Matrix.html) TRS([Vec3]({{site.url}}/Pages/Reference/Vec3.html) translation, [Quat]({{site.url}}/Pages/Reference/Quat.html) rotation, [Vec3]({{site.url}}/Pages/Reference/Vec3.html) scale)
</div>

|  |  |
|--|--|
|[Vec3]({{site.url}}/Pages/Reference/Vec3.html) translation|Move an object by this amount.|
|[Quat]({{site.url}}/Pages/Reference/Quat.html) rotation|A Quaternion describing the rotation for this transform.|
|[Vec3]({{site.url}}/Pages/Reference/Vec3.html) scale|How much larger or smaller this transform makes things. Vec3.One is a good             default, as Vec3.Zero will shrink it to nothing!|
|RETURNS: [Matrix]({{site.url}}/Pages/Reference/Matrix.html)|A Matrix that combines translation, roatation, and scale information into a single Matrix!|

Transform, Rotate, Scale. Creates a transform Matrix using all these components!


